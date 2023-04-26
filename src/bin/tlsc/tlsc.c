#include "client.h"
#include "clientopts.h"
#include "config.h"
#include "connection.h"
#include "event.h"
#include "daemon.h"
#include "daemonopts.h"
#include "log.h"
#include "server.h"
#include "serveropts.h"
#include "service.h"
#include "threadopts.h"
#include "threadpool.h"
#include "tlsc.h"
#include "util.h"

#include <stdlib.h>
#include <syslog.h>

#ifndef LOGIDENT
#define LOGIDENT "tlsc"
#endif

#define SERVCHUNK 16

typedef struct ServCtx
{
    Server *server;
    const TunnelConfig *tc;
} ServCtx;

typedef struct ConnCtx
{
    Connection *client;
    Connection *service;
    const char *chost;
    const char *shost;
    int connected;
} ConnCtx;

static DaemonOpts daemonOpts = {
    .pidfile = 0,
    .uid = -1,
    .gid = -1,
    .daemonize = 0
};

static ThreadOpts threadOpts = {
    .nThreads = 0,
    .maxThreads = 16,
    .nPerCpu = 1,
    .defNThreads = 4,
    .queueLen = 0,
    .maxQueueLen = 32,
    .minQueueLen = 16,
    .qLenPerThread = 2
};

static const Config *cfg;
static ServCtx *servers = 0;
static size_t servcapa = 0;
static size_t servsize = 0;

static void datareceived(void *receiver, void *sender, void *args)
{
    (void)sender;

    Connection *c = receiver;
    DataReceivedEventArgs *ea = args;
    ea->handling = 1;
    Connection_write(c, ea->buf, ea->size, ea);
}

static void datasent(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    Connection *c = receiver;
    Connection_confirmDataReceived(c);
}

static void logconnected(ConnCtx *ctx)
{
    if (ctx->chost && ctx->shost && ctx->connected)
    {
	Log_fmt(L_INFO, "Tlsc: connected %s:%d -> %s:%d",
		ctx->chost, Connection_remotePort(ctx->client),
		ctx->shost, Connection_remotePort(ctx->service));
    }
}

static void nameresolved(void *receiver, void *sender, void *args)
{
    (void)args;

    ConnCtx *ctx = receiver;
    Connection *c = sender;

    if (c == ctx->client) ctx->chost = Connection_remoteHost(c);
    else ctx->shost = Connection_remoteHost(c);

    logconnected(ctx);
}

static void connected(void *receiver, void *sender, void *args)
{
    (void)args;

    ConnCtx *ctx = receiver;
    Connection *sv = sender;
    Connection *cl = ctx->client;

    Event_register(Connection_dataReceived(cl), sv, datareceived, 0);
    Event_register(Connection_dataReceived(sv), cl, datareceived, 0);
    Event_register(Connection_dataSent(cl), sv, datasent, 0);
    Event_register(Connection_dataSent(sv), cl, datasent, 0);

    ctx->connected = 1;
    if (Config_numerichosts(cfg))
    {
	ctx->chost = Connection_remoteAddr(cl);
	ctx->shost = Connection_remoteAddr(sv);
    }
    logconnected(ctx);

    Connection_activate(cl);
}

static void connclosed(void *receiver, void *sender, void *args)
{
    ConnCtx *ctx = receiver;
    Connection *c = sender;
    Connection *o = (c == ctx->client) ? ctx->service : ctx->client;

    Event_unregister(Connection_closed(o), ctx, connclosed, 0);
    Event_unregister(Connection_closed(c), ctx, connclosed, 0);
    if (args)
    {
	Event_unregister(Connection_dataReceived(c), o, datareceived, 0);
	Event_unregister(Connection_dataReceived(o), c, datareceived, 0);
	Event_unregister(Connection_dataSent(c), o, datasent, 0);
	Event_unregister(Connection_dataSent(o), c, datasent, 0);
	const char *chost = Connection_remoteHost(c);
	if (!chost) chost = Connection_remoteAddr(c);
	const char *ohost = Connection_remoteHost(o);
	if (!ohost) ohost = Connection_remoteAddr(o);
	Log_fmt(L_INFO, "Tlsc: connection %s:%d <-> %s:%d closed",
		chost, Connection_remotePort(c),
		ohost, Connection_remotePort(o));
    }
    else
    {
	Event_unregister(Connection_connected(c), ctx, connected, 0);
    }

    Connection_close(o, 0);
    free(ctx);
}

static void svConnCreated(void *receiver, Connection *sv)
{
    Connection *cl = receiver;

    if (!sv)
    {
	Connection_close(cl, 0);
	return;
    }

    ConnCtx *ctx = xmalloc(sizeof *ctx);
    ctx->client = cl;
    ctx->service = sv;
    ctx->chost = 0;
    ctx->shost = 0;
    ctx->connected = 0;

    if (!Config_numerichosts(cfg))
    {
	Event_register(Connection_nameResolved(cl), ctx, nameresolved, 0);
	Event_register(Connection_nameResolved(sv), ctx, nameresolved, 0);
    }
    Event_register(Connection_closed(cl), ctx, connclosed, 0);
    Event_register(Connection_closed(sv), ctx, connclosed, 0);
    Event_register(Connection_connected(sv), ctx, connected, 0);
}

static void newclient(void *receiver, void *sender, void *args)
{
    (void)sender;

    ServCtx *ctx = receiver;
    Connection *cl = args;

    ClientOpts co = {
	.remotehost = TunnelConfig_remotehost(ctx->tc),
	.tls_certfile = TunnelConfig_certfile(ctx->tc),
	.tls_keyfile = TunnelConfig_keyfile(ctx->tc),
	.proto = CP_ANY,
	.port = TunnelConfig_remoteport(ctx->tc),
	.numerichosts = Config_numerichosts(cfg),
	.tls = 1
    };
    if (Connection_createTcpClientAsync(&co, cl, svConnCreated) < 0)
    {
	Connection_close(cl, 0);
    }
}

static void svstartup(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;

    StartupEventArgs *ea = args;

    const TunnelConfig *tc = Config_tunnel(cfg);
    while (tc)
    {
	ServerOpts so = { 0 };
	so.bindhost[0] = TunnelConfig_bindhost(tc);
	so.port = TunnelConfig_bindport(tc);
	so.numerichosts = Config_numerichosts(cfg);
	so.connwait = 1;
	Server *server = Server_createTcp(&so);
	if (!server)
	{
	    ea->rc = EXIT_FAILURE;
	    return;
	}
	if (servcapa == servsize)
	{
	    servcapa += SERVCHUNK;
	    servers = xrealloc(servers, servcapa * sizeof *servers);
	}
	servers[servsize].server = server;
	servers[servsize].tc = tc;
	Event_register(Server_clientConnected(server), &servers[servsize++],
		newclient, 0);
	tc = TunnelConfig_next(tc);
    }

    if (Config_daemonize(cfg))
    {
	Log_setAsync(1);
	Log_setSyslogLogger(LOGIDENT, LOG_DAEMON, 0);
	daemon_launched();
    }
}

static void svshutdown(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;
    (void)args;

    for (size_t i = 0; i < servsize; ++i)
    {
	Server_destroy(servers[i].server);
    }
    free(servers);
    servers = 0;
    servcapa = 0;
    servsize = 0;
}

static int daemonrun(void *data)
{
    (void)data;

    int rc = EXIT_FAILURE;

    if (Service_init(&daemonOpts) >= 0)
    {
	Service_setTickInterval(1000);
	Event_register(Service_startup(), 0, svstartup, 0);
	Event_register(Service_shutdown(), 0, svshutdown, 0);
	if (ThreadPool_init(&threadOpts) >= 0)
	{
	    rc = Service_run();
	    ThreadPool_done();
	}
	Service_done();
    }

    return rc;
}

SOLOCAL int Tlsc_run(const Config *config)
{
    cfg = config;

    if (Config_verbose(cfg))
    {
	Log_setMaxLogLevel(L_DEBUG);
    }

    if (Config_daemonize(cfg))
    {
	Log_setSyslogLogger(LOGIDENT, LOG_DAEMON, 1);
	daemonOpts.pidfile = Config_pidfile(cfg);
	daemonOpts.uid = Config_uid(cfg);
	daemonOpts.gid = Config_gid(cfg);
	daemonOpts.daemonize = 1;
	return daemon_run(daemonrun, 0, daemonOpts.pidfile, 1);
    }
    else
    {
	Log_setFileLogger(stderr);
	return daemonrun(0);
    }
}


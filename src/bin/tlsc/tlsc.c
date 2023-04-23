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
static Server **servers = 0;
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

static void connected(void *receiver, void *sender, void *args)
{
    (void)args;

    Connection *cl = receiver;
    Connection *sv = sender;

    Event_register(Connection_dataReceived(cl), sv, datareceived, 0);
    Event_register(Connection_dataReceived(sv), cl, datareceived, 0);
    Event_register(Connection_dataSent(cl), sv, datasent, 0);
    Event_register(Connection_dataSent(sv), cl, datasent, 0);

    Connection_activate(cl);
}

static void connclosed(void *receiver, void *sender, void *args)
{
    (void)args;

    Connection *c = receiver;
    Event_unregister(Connection_closed(c), sender, connclosed, 0);
    Connection_close(c, 0);
}

static void newclient(void *receiver, void *sender, void *args)
{
    (void)sender;

    const TunnelConfig *tc = receiver;
    Connection *cl = args;

    ClientOpts co = {
	.remotehost = TunnelConfig_remotehost(tc),
	.tls_certfile = TunnelConfig_certfile(tc),
	.tls_keyfile = TunnelConfig_keyfile(tc),
	.proto = CP_ANY,
	.port = TunnelConfig_remoteport(tc),
	.numerichosts = Config_numerichosts(cfg),
	.tls = 1
    };
    Connection *sv = Connection_createTcpClient(&co);
    if (!sv)
    {
	Connection_close(cl, 0);
	return;
    }

    Event_register(Connection_closed(cl), sv, connclosed, 0);
    Event_register(Connection_closed(sv), cl, connclosed, 0);
    Event_register(Connection_connected(sv), cl, connected, 0);
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
	Event_register(Server_clientConnected(server), (void *)tc,
		newclient, 0);
	if (servcapa == servsize)
	{
	    servcapa += SERVCHUNK;
	    servers = xrealloc(servers, servcapa * sizeof *servers);
	}
	servers[servsize++] = server;
	tc = TunnelConfig_next(tc);
    }
    Log_setAsync(1);
    Log_setSyslogLogger(LOGIDENT, LOG_DAEMON, 0);
    daemon_launched();
}

static void svshutdown(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;
    (void)args;

    for (size_t i = 0; i < servsize; ++i)
    {
	Server_destroy(servers[i]);
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


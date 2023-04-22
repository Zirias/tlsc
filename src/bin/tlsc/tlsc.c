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

#include <stdlib.h>
#include <syslog.h>

#ifndef PIDFILE
#define PIDFILE "/var/run/tlsc.pid"
#endif

#ifndef LOGIDENT
#define LOGIDENT "tlsc"
#endif

static DaemonOpts daemonOpts = {
    .started = 0,
    .pidfile = PIDFILE,
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
static Server *server = 0;

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
}

static void connclosed(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    Connection *c = receiver;
    Event_unregister(Connection_closed(c), sender, connclosed, 0);
    Connection_close(c, 0);
}

static void newclient(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;

    Connection *cl = args;

    ClientOpts co = { 0 };
    co.remotehost = TunnelConfig_remotehost(Config_tunnel(cfg));
    co.port = TunnelConfig_remoteport(Config_tunnel(cfg));
    co.numerichosts = 1;
    co.tls = 1;
    Connection *sv = Connection_createTcpClient(&co);

    Event_register(Connection_closed(cl), sv, connclosed, 0);
    Event_register(Connection_closed(sv), cl, connclosed, 0);
    Event_register(Connection_connected(sv), cl, connected, 0);
}

static void svstartup(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;

    StartupEventArgs *ea = args;

    ServerOpts so = { 0 };
    so.bindhost[0] = TunnelConfig_bindhost(Config_tunnel(cfg));
    so.port = TunnelConfig_bindport(Config_tunnel(cfg));
    so.numerichosts = 1;
    server = Server_createTcp(&so);
    if (!server)
    {
	ea->rc = EXIT_FAILURE;
	return;
    }

    Event_register(Server_clientConnected(server), 0, newclient, 0);
}

static void svshutdown(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;
    (void)args;

    Server_destroy(server);
    server = 0;
}

static void daemonized(void)
{
    Log_setSyslogLogger(LOGIDENT, LOG_DAEMON, 0);
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
#ifdef DEBUG
    Log_setMaxLogLevel(L_DEBUG);
#endif
    if (Config_daemonize(cfg))
    {
	Log_setSyslogLogger(LOGIDENT, LOG_DAEMON, 1);
	daemonOpts.daemonize = 1;
	daemonOpts.started = daemonized;
	return daemon_run(daemonrun, 0, daemonOpts.pidfile, 1);
    }
    else
    {
	Log_setFileLogger(stderr);
	return daemonrun(0);
    }
}


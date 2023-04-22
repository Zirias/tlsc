#include "config.h"
#include "event.h"
#include "daemon.h"
#include "daemonopts.h"
#include "log.h"
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

static void startup(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;

    StartupEventArgs *ea = args;
}

static void shutdown(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;
    (void)args;
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
	Event_register(Service_startup(), 0, startup, 0);
	Event_register(Service_shutdown(), 0, shutdown, 0);
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
    if (cfg->daemonize)
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


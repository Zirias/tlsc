#define _DEFAULT_SOURCE

#include "daemonopts.h"
#include "event.h"
#include "log.h"
#include "service.h"

#include <grp.h>
#include <setjmp.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

static const DaemonOpts *opts;
static Event *readyRead;
static Event *readyWrite;
static Event *startup;
static Event *shutdown;
static Event *tick;
static Event *eventsDone;

static fd_set readfds;
static fd_set writefds;
static int nread;
static int nwrite;
static int nfds;
static int running;

static volatile sig_atomic_t shutdownRequest;
static volatile sig_atomic_t timerTick;

static int shutdownRef;
static int shutdownTicks;

static struct itimerval timer;
static struct itimerval otimer;

static jmp_buf panicjmp;
static PanicHandler panicHandlers[MAXPANICHANDLERS];
static int numPanicHandlers;

static void handlesig(int signum);
static void tryReduceNfds(int id);

static void handlesig(int signum)
{
    if (signum == SIGALRM) timerTick = 1;
    else shutdownRequest = 1;
}

static void tryReduceNfds(int id)
{
    if (!nread && !nwrite)
    {
	nfds = 0;
    }
    else if (id+1 >= nfds)
    {
	int fd;
	for (fd = id; fd >= 0; --fd)
	{
	    if (FD_ISSET(fd, &readfds) || FD_ISSET(fd, &writefds))
	    {
		break;
	    }
	}
	nfds = fd+1;
    }
}

SOLOCAL int Service_init(const DaemonOpts *options)
{
    if (opts) return -1;
    opts = options;
    readyRead = Event_create(0);
    readyWrite = Event_create(0);
    startup = Event_create(0);
    shutdown = Event_create(0);
    tick = Event_create(0);
    eventsDone = Event_create(0);
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    nread = 0;
    nwrite = 0;
    nfds = 0;
    running = 0;
    shutdownRequest = 0;
    timerTick = 0;
    shutdownRef = -1;
    shutdownTicks = -1;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;
    return 0;
}

SOLOCAL Event *Service_readyRead(void)
{
    return readyRead;
}

SOLOCAL Event *Service_readyWrite(void)
{
    return readyWrite;
}

SOLOCAL Event *Service_startup(void)
{
    return startup;
}

SOLOCAL Event *Service_shutdown(void)
{
    return shutdown;
}

SOLOCAL Event *Service_tick(void)
{
    return tick;
}

SOLOCAL Event *Service_eventsDone(void)
{
    return eventsDone;
}

SOLOCAL void Service_registerRead(int id)
{
    if (FD_ISSET(id, &readfds)) return;
    FD_SET(id, &readfds);
    ++nread;
    if (id >= nfds) nfds = id+1;
}

SOLOCAL void Service_unregisterRead(int id)
{
    if (!FD_ISSET(id, &readfds)) return;
    FD_CLR(id, &readfds);
    --nread;
    tryReduceNfds(id);
}

SOLOCAL void Service_registerWrite(int id)
{
    if (FD_ISSET(id, &writefds)) return;
    FD_SET(id, &writefds);
    ++nwrite;
    if (id >= nfds) nfds = id+1;
}

SOLOCAL void Service_unregisterWrite(int id)
{
    if (!FD_ISSET(id, &writefds)) return;
    FD_CLR(id, &writefds);
    --nwrite;
    tryReduceNfds(id);
}

SOLOCAL void Service_registerPanic(PanicHandler handler)
{
    if (numPanicHandlers >= MAXPANICHANDLERS) return;
    panicHandlers[numPanicHandlers++] = handler;
}

SOLOCAL void Service_unregisterPanic(PanicHandler handler)
{
    for (int i = 0; i < numPanicHandlers; ++i)
    {
	if (panicHandlers[i] == handler)
	{
	    if (--numPanicHandlers > i)
	    {
		memmove(panicHandlers + i, panicHandlers + i + 1,
			(numPanicHandlers - i) * sizeof *panicHandlers);
	    }
	    break;
	}
    }
}

SOLOCAL int Service_setTickInterval(unsigned msec)
{
    timer.it_interval.tv_sec = msec / 1000U;
    timer.it_interval.tv_usec = 1000U * (msec % 1000U);
    timer.it_value.tv_sec = msec / 1000U;
    timer.it_value.tv_usec = 1000U * (msec % 1000U);
    if (running) return setitimer(ITIMER_REAL, &timer, 0);
    return 0;
}

SOLOCAL int Service_run(void)
{
    if (!opts) return -1;

    int rc = EXIT_FAILURE;
    if (opts->uid != -1 && geteuid() == 0)
    {
	if (opts->daemonize)
	{
	    chown(opts->pidfile, opts->uid, opts->gid);
	}
	if (opts->gid != -1)
	{
	    gid_t gid = opts->gid;
	    if (setgroups(1, &gid) < 0 || setgid(gid) < 0)
	    {
		Log_msg(L_ERROR, "cannot set specified group");
		return rc;
	    }
	}
	if (setuid(opts->uid) < 0)
	{
	    Log_msg(L_ERROR, "cannot set specified user");
	    return rc;
	}
    }

    struct sigaction handler;
    memset(&handler, 0, sizeof handler);
    handler.sa_handler = handlesig;
    sigemptyset(&handler.sa_mask);
    sigaddset(&handler.sa_mask, SIGTERM);
    sigaddset(&handler.sa_mask, SIGINT);
    sigaddset(&handler.sa_mask, SIGALRM);
    sigset_t mask;

    if (sigprocmask(SIG_BLOCK, &handler.sa_mask, &mask) < 0)
    {
	Log_msg(L_ERROR, "cannot set signal mask");
	return rc;
    }

    if (sigaction(SIGTERM, &handler, 0) < 0)
    {
	Log_msg(L_ERROR, "cannot set signal handler for SIGTERM");
	goto done;
    }

    if (sigaction(SIGINT, &handler, 0) < 0)
    {
	Log_msg(L_ERROR, "cannot set signal handler for SIGINT");
	goto done;
    }

    if (sigaction(SIGALRM, &handler, 0) < 0)
    {
	Log_msg(L_ERROR, "cannot set signal handler for SIGALRM");
	goto done;
    }

    if (setitimer(ITIMER_REAL, &timer, &otimer) < 0)
    {
	Log_msg(L_ERROR, "cannot set periodic timer");
	goto done;
    }

    StartupEventArgs sea = { EXIT_SUCCESS };
    Event_raise(startup, 0, &sea);
    rc = sea.rc;
    if (rc != EXIT_SUCCESS) goto done;

    running = 1;
    Log_msg(L_INFO, "service started");

    if (setjmp(panicjmp) < 0) goto shutdown;

    while (shutdownRef != 0)
    {
	Event_raise(eventsDone, 0, 0);
	fd_set rfds;
	fd_set wfds;
	fd_set *r = 0;
	fd_set *w = 0;
	if (nread)
	{
	    memcpy(&rfds, &readfds, sizeof rfds);
	    r = &rfds;
	}
	if (nwrite)
	{
	    memcpy(&wfds, &writefds, sizeof wfds);
	    w = &wfds;
	}
	int src;
	if (!shutdownRequest) src = pselect(nfds, r, w, 0, 0, &mask);
	if (shutdownRequest)
	{
	    shutdownRequest = 0;
	    shutdownRef = 0;
	    shutdownTicks = 5;
	    Event_raise(shutdown, 0, 0);
	    continue;
	}
	if (timerTick)
	{
	    timerTick = 0;
	    if (shutdownTicks > 0 && !--shutdownTicks)
	    {
		shutdownRef = 0;
		break;
	    }
	    Event_raise(tick, 0, 0);
	    continue;
	}
	if (src < 0)
	{
	    Log_msg(L_ERROR, "pselect() failed");
	    rc = EXIT_FAILURE;
	    break;
	}
	if (w) for (int i = 0; src > 0 && i < nfds; ++i)
	{
	    if (FD_ISSET(i, w))
	    {
		--src;
		Event_raise(readyWrite, i, 0);
	    }
	}
	if (r) for (int i = 0; src > 0 && i < nfds; ++i)
	{
	    if (FD_ISSET(i, r))
	    {
		--src;
		Event_raise(readyRead, i, 0);
	    }
	}
    }

shutdown:
    running = 0;
    Log_msg(L_INFO, "service shutting down");

done:
    if (sigprocmask(SIG_SETMASK, &mask, 0) < 0)
    {
	Log_msg(L_ERROR, "cannot restore original signal mask");
	rc = EXIT_FAILURE;
    }

    if (setitimer(ITIMER_REAL, &otimer, 0) < 0)
    {
	Log_msg(L_ERROR, "cannot restore original periodic timer");
	rc = EXIT_FAILURE;
    }

    return rc;
}

SOLOCAL void Service_quit(void)
{
    shutdownRequest = 1;
}

SOLOCAL void Service_shutdownLock(void)
{
    if (shutdownRef == 0) Service_setTickInterval(1000);
    if (shutdownRef >= 0) ++shutdownRef;
}

SOLOCAL void Service_shutdownUnlock(void)
{
    if (shutdownRef > 0) --shutdownRef;
}

SOLOCAL void Service_panic(const char *msg)
{
    if (running) for (int i = 0; i < numPanicHandlers; ++i)
    {
	panicHandlers[i](msg);
    }
    Log_setAsync(0);
    Log_msg(L_FATAL, msg);
    if (running) longjmp(panicjmp, -1);
    else abort();
}

SOLOCAL void Service_done(void)
{
    if (!opts) return;
    Event_destroy(eventsDone);
    Event_destroy(tick);
    Event_destroy(shutdown);
    Event_destroy(startup);
    Event_destroy(readyWrite);
    Event_destroy(readyRead);
    opts = 0;
    shutdown = 0;
    startup = 0;
    readyWrite = 0;
    readyRead = 0;
}


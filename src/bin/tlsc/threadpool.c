#define _DEFAULT_SOURCE

#include "event.h"
#include "log.h"
#include "service.h"
#include "threadopts.h"
#include "threadpool.h"
#include "util.h"

#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

struct ThreadJob
{
    ThreadProc proc;
    void *arg;
    Event *finished;
    const char *panicmsg;
    int hasCompleted;
    int timeoutTicks;
};

typedef struct Thread
{
    ThreadJob *job;
    pthread_t handle;
    pthread_mutex_t startlock;
    pthread_mutex_t donelock;
    pthread_cond_t start;
    pthread_cond_t done;
    int pipefd[2];
    int stoprq;
} Thread;

static Thread *threads;
static ThreadJob **jobQueue;
static pthread_mutex_t queuelock;
static int nthreads;
static int queuesize;
static int queueAvail;
static int nextIdx;
static int lastIdx;

static thread_local int mainthread;
static thread_local jmp_buf panicjmp;
static thread_local const char *panicmsg;
static thread_local volatile sig_atomic_t jobcanceled;

static Thread *availableThread(void);
static void checkThreadJobs(void *receiver, void *sender, void *args);
static ThreadJob *dequeueJob(void);
static int enqueueJob(ThreadJob *job) ATTR_NONNULL((1));
static void panicHandler(const char *msg) ATTR_NONNULL((1));
static void startThreadJob(Thread *t, ThreadJob *j)
    ATTR_NONNULL((1)) ATTR_NONNULL((2));
static void stopThreads(int nthr);
static void threadJobDone(void *receiver, void *sender, void *args);
static void *worker(void *arg);
static void workerInterrupt(int signum);

static void workerInterrupt(int signum)
{
    (void) signum;
    jobcanceled = 1;
}

static void *worker(void *arg)
{
    Thread *t = arg;

    struct sigaction handler;
    memset(&handler, 0, sizeof handler);
    handler.sa_handler = workerInterrupt;
    sigemptyset(&handler.sa_mask);
    sigaddset(&handler.sa_mask, SIGUSR1);
    if (sigaction(SIGUSR1, &handler, 0) < 0) return 0;
    if (pthread_sigmask(SIG_UNBLOCK, &handler.sa_mask, 0) < 0) return 0;

    if (pthread_mutex_lock(&t->startlock) < 0) return 0;
    t->stoprq = 0;
    while (!t->stoprq)
    {
	jobcanceled = 0;
	pthread_cond_wait(&t->start, &t->startlock);
	if (t->stoprq) break;
	if (!setjmp(panicjmp)) t->job->proc(t->job->arg);
	else t->job->panicmsg = panicmsg;
	write(t->pipefd[1], "0", 1);
	pthread_mutex_lock(&t->donelock);
	pthread_cond_signal(&t->done);
	pthread_mutex_unlock(&t->donelock);
    }

    pthread_mutex_unlock(&t->startlock);
    return 0;
}

SOLOCAL ThreadJob *ThreadJob_create(
	ThreadProc proc, void *arg, int timeoutTicks)
{
    ThreadJob *self = xmalloc(sizeof *self);
    self->proc = proc;
    self->arg = arg;
    self->finished = Event_create(self);
    self->panicmsg = 0;
    self->timeoutTicks = timeoutTicks;
    self->hasCompleted = 1;
    return self;
}

SOLOCAL Event *ThreadJob_finished(ThreadJob *self)
{
    return self->finished;
}

SOLOCAL int ThreadJob_hasCompleted(const ThreadJob *self)
{
    return self->hasCompleted;
}

SOLOCAL void ThreadJob_destroy(ThreadJob *self)
{
    if (!self) return;
    Event_destroy(self->finished);
    free(self);
}

SOLOCAL int ThreadJob_canceled(void)
{
    return (int)jobcanceled;
}

static void stopThreads(int nthr)
{
    for (int i = 0; i < nthr; ++i)
    {
	if (pthread_kill(threads[i].handle, 0) >= 0)
	{
	    if (pthread_mutex_trylock(&threads[i].startlock) != 0)
	    {
		threads[i].stoprq = 1;
		pthread_kill(threads[i].handle, SIGUSR1);
		pthread_cond_wait(&threads[i].done, &threads[i].donelock);
		pthread_mutex_unlock(&threads[i].donelock);
	    }
	    else
	    {
		threads[i].stoprq = 1;
		pthread_cond_signal(&threads[i].start);
		pthread_mutex_unlock(&threads[i].startlock);
	    }
	}
	pthread_join(threads[i].handle, 0);
	close(threads[i].pipefd[0]);
	close(threads[i].pipefd[1]);
	pthread_cond_destroy(&threads[i].done);
	pthread_mutex_destroy(&threads[i].donelock);
	pthread_cond_destroy(&threads[i].start);
	pthread_mutex_destroy(&threads[i].startlock);
    }
}

static int enqueueJob(ThreadJob *job)
{
    int rc = -1;
    pthread_mutex_lock(&queuelock);
    if (!queueAvail) goto done;
    rc = 0;
    jobQueue[nextIdx++] = job;
    --queueAvail;
    if (nextIdx == queuesize) nextIdx = 0;
done:
    pthread_mutex_unlock(&queuelock);
    return rc;
}

static ThreadJob *dequeueJob(void)
{
    ThreadJob *job = 0;
    pthread_mutex_lock(&queuelock);
    while (!job)
    {
	if (queueAvail == queuesize) break;
	job = jobQueue[lastIdx];
	jobQueue[lastIdx++] = 0;
	++queueAvail;
	if (lastIdx == queuesize) lastIdx = 0;
    }
    pthread_mutex_unlock(&queuelock);
    return job;
}

static Thread *availableThread(void)
{
    for (int i = 0; i < nthreads; ++i)
    {
	if (!threads[i].job) return threads+i;
    }
    return 0;
}

static void startThreadJob(Thread *t, ThreadJob *j)
{
    if (pthread_kill(t->handle, 0) == ESRCH)
    {
	pthread_join(t->handle, 0);
	Log_msg(L_WARNING, "threadpool: restarting failed thread");
	if (pthread_create(&t->handle, 0, worker, t) < 0)
	{
	    Log_msg(L_FATAL, "threadpool: error restarting thread");
	    Service_quit();
	}
	return;
    }
    pthread_mutex_lock(&t->startlock);
    t->job = j;
    Service_registerRead(t->pipefd[0]);
    pthread_cond_signal(&t->start);
    pthread_mutex_lock(&t->donelock);
    pthread_mutex_unlock(&t->startlock);
}

static void threadJobDone(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    Thread *t = receiver;
    Service_unregisterRead(t->pipefd[0]);
    char buf[2];
    read(t->pipefd[0], buf, sizeof buf);
    pthread_cond_wait(&t->done, &t->donelock);
    if (t->job->panicmsg)
    {
	const char *msg = t->job->panicmsg;
	ThreadJob_destroy(t->job);
	t->job = 0;
	pthread_mutex_unlock(&t->donelock);
	Service_panic(msg);
    }
    Event_raise(t->job->finished, 0, t->job->arg);
    ThreadJob_destroy(t->job);
    t->job = 0;
    pthread_mutex_unlock(&t->donelock);
    ThreadJob *next = dequeueJob();
    if (next) startThreadJob(t, next);
}

static void checkThreadJobs(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;
    (void)args;

    for (int i = 0; i < nthreads; ++i)
    {
	if (threads[i].job && threads[i].job->timeoutTicks
		&& !--threads[i].job->timeoutTicks)
	{
	    pthread_kill(threads[i].handle, SIGUSR1);
	    threads[i].job->hasCompleted = 0;
	}
    }
}

static void panicHandler(const char *msg)
{
    if (!threads || mainthread) return;
    panicmsg = msg;
    longjmp(panicjmp, -1);
}

SOLOCAL int ThreadPool_init(const ThreadOpts *opts)
{
    sigset_t blockmask;
    sigset_t mask;
    sigfillset(&blockmask);
    int rc = -1;
    
    if (threads) return rc;

    if (sigprocmask(SIG_BLOCK, &blockmask, &mask) < 0)
    {
	Log_msg(L_ERROR, "threadpool: cannot set signal mask");
	return rc;
    }

    if (opts->nThreads)
    {
	nthreads = opts->nThreads;
    }
    else
    {
#ifdef _SC_NPROCESSORS_CONF
	long ncpu = sysconf(_SC_NPROCESSORS_CONF);
	if (ncpu >= 1)
	{
	    if (ncpu <= (opts->maxThreads / opts->nPerCpu))
	    {
		nthreads = opts->nPerCpu * ncpu;
	    }
	    else nthreads = opts->maxThreads;
	}
	else nthreads = opts->defNThreads;
#else
	nthreads = opts->defNThreads;
#endif
    }
    if (opts->queueLen)
    {
	queuesize = opts->queueLen;
    }
    else if (nthreads <= (opts->maxQueueLen / opts->qLenPerThread))
    {
	queuesize = opts->qLenPerThread * nthreads;
	if (queuesize < opts->minQueueLen) queuesize = opts->minQueueLen;
    }
    else queuesize = opts->maxQueueLen;

    Log_fmt(L_DEBUG, "threadpool: starting with %d threads and a queue for "
	    "%d jobs", nthreads, queuesize);

    threads = xmalloc(nthreads * sizeof *threads);
    memset(threads, 0, nthreads * sizeof *threads);
    jobQueue = xmalloc(queuesize * sizeof *jobQueue);
    memset(jobQueue, 0, queuesize * sizeof *jobQueue);

    for (int i = 0; i < nthreads; ++i)
    {
	if (pthread_mutex_init(&threads[i].startlock, 0) < 0)
	{
	    Log_msg(L_ERROR, "threadpool: error creating mutex");
	    goto rollback;
	}
	if (pthread_cond_init(&threads[i].start, 0) < 0)
	{
	    Log_msg(L_ERROR, "threadpool: error creating condition variable");
	    goto rollback_startlock;
	}
	if (pthread_mutex_init(&threads[i].donelock, 0) < 0)
	{
	    Log_msg(L_ERROR, "threadpool: error creating mutex");
	    goto rollback_start;
	}
	if (pthread_cond_init(&threads[i].done, 0) < 0)
	{
	    Log_msg(L_ERROR, "threadpool: error creating condition variable");
	    goto rollback_donelock;
	}
	if (pipe(threads[i].pipefd) < 0)
	{
	    Log_msg(L_ERROR, "threadpool: error creating pipe");
	    goto rollback_done;
	}
	Event_register(Service_readyRead(), threads+i, threadJobDone,
		threads[i].pipefd[0]);
	if (pthread_create(&threads[i].handle, 0, worker, threads+i) < 0)
	{
	    Log_msg(L_ERROR, "threadpool: error creating thread");
	    Event_unregister(Service_readyRead(), threads+i, threadJobDone,
		    threads[i].pipefd[0]);
	    goto rollback_pipe;
	}
	continue;

rollback_pipe:
	close(threads[i].pipefd[0]);
	close(threads[i].pipefd[1]);
rollback_done:
	pthread_cond_destroy(&threads[i].done);
rollback_donelock:
	pthread_mutex_destroy(&threads[i].donelock);
rollback_start:
	pthread_cond_destroy(&threads[i].start);
rollback_startlock:
	pthread_mutex_destroy(&threads[i].startlock);
rollback:
	stopThreads(i);
	goto done;
    }
    rc = 0;
    Event_register(Service_tick(), 0, checkThreadJobs, 0);
    queueAvail = queuesize;
    nextIdx = 0;
    lastIdx = 0;
    if (pthread_mutex_init(&queuelock, 0) < 0)
    {
	stopThreads(nthreads);
	rc = -1;
    }

done:
    if (sigprocmask(SIG_SETMASK, &mask, 0) < 0)
    {
	Log_msg(L_ERROR, "threadpool: cannot restore signal mask");
	if (rc == 0) stopThreads(nthreads);
	rc = -1;
    }

    if (rc == 0)
    {
	mainthread = 1;
	Service_registerPanic(panicHandler);
    }
    else
    {
	free(threads);
	threads = 0;
	free(jobQueue);
	jobQueue = 0;
	queueAvail = 0;
    }

    return rc;
}

SOLOCAL int ThreadPool_active(void)
{
    return !!threads;
}

SOLOCAL int ThreadPool_enqueue(ThreadJob *job)
{
    if (mainthread && threads)
    {
	Thread *t = availableThread();
	if (t)
	{
	    startThreadJob(t, job);
	    return 0;
	}
    }
    return enqueueJob(job);
}

SOLOCAL void ThreadPool_cancel(ThreadJob *job)
{
    if (threads)
    {
	for (int i = 0; i < nthreads; ++i)
	{
	    if (threads[i].job == job)
	    {
		pthread_kill(threads[i].handle, SIGUSR1);
		threads[i].job->hasCompleted = 0;
		return;
	    }
	}
    }
    if (queueAvail != queuesize)
    {
	int i = lastIdx;
	do
	{
	    if (jobQueue[i] == job)
	    {
		job->hasCompleted = 0;
		Event_raise(job->finished, 0, job->arg);
		ThreadJob_destroy(job);
		jobQueue[i] = 0;
		return;
	    }
	    if (++i == queuesize) i = 0;
	} while ( i != nextIdx);
    }
}

SOLOCAL void ThreadPool_done(void)
{
    if (!threads) return;
    stopThreads(nthreads);
    free(threads);
    threads = 0;
    pthread_mutex_destroy(&queuelock);
    for (int i = 0; i < queuesize; ++i) ThreadJob_destroy(jobQueue[i]);
    free(jobQueue);
    jobQueue = 0;
    queueAvail = 0;
    Service_unregisterPanic(panicHandler);
    mainthread = 0;
}

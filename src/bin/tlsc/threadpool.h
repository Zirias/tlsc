#ifndef TLSC_THREADPOOL_H
#define TLSC_THREADPOOL_H

#include "decl.h"

C_CLASS_DECL(Event);
C_CLASS_DECL(ThreadJob);
C_CLASS_DECL(ThreadOpts);

typedef void (*ThreadProc)(void *arg);

ThreadJob *ThreadJob_create(ThreadProc proc, void *arg, int timeoutTicks)
    ATTR_NONNULL((1)) ATTR_RETNONNULL;
Event *ThreadJob_finished(ThreadJob *self) CMETHOD ATTR_RETNONNULL ATTR_PURE;
int ThreadJob_hasCompleted(const ThreadJob *self) CMETHOD ATTR_PURE;
void ThreadJob_destroy(ThreadJob *self);
int ThreadJob_canceled(void);

int ThreadPool_init(const ThreadOpts *opts) ATTR_NONNULL((1));
int ThreadPool_active(void);
int ThreadPool_enqueue(ThreadJob *job) ATTR_NONNULL((1));
void ThreadPool_cancel(ThreadJob *job) ATTR_NONNULL((1));
void ThreadPool_done(void);

#endif

#ifndef TLSC_THREADOPTS_H
#define TLSC_THREADOPTS_H

typedef struct ThreadOpts
{
    int nThreads;
    int maxThreads;
    int nPerCpu;
    int defNThreads;
    int queueLen;
    int maxQueueLen;
    int minQueueLen;
    int qLenPerThread;
} ThreadOpts;

#endif

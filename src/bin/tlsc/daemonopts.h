#ifndef TLSC_DAEMONOPTS_H
#define TLSC_DAEMONOPTS_H

typedef struct DaemonOpts
{
    const char *pidfile;
    long uid;
    long gid;
    int daemonize;
} DaemonOpts;

#endif

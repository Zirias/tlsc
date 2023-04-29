#ifndef TLSC_SERVEROPTS_H
#define TLSC_SERVEROPTS_H

#include "proto.h"

#ifndef MAXBINDS
#define MAXBINDS 4
#endif

typedef struct ServerOpts
{
    const char *bindhost[MAXBINDS];
    Proto proto;
    int port;
    int numerichosts;
    int connwait;
} ServerOpts;

#endif

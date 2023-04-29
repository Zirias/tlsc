#ifndef TLSC_CLIENTOPTS_H
#define TLSC_CLIENTOPTS_H

#include "proto.h"

typedef struct ClientOpts
{
    const char *remotehost;
    const char *tls_certfile;
    const char *tls_keyfile;
    Proto proto;
    int port;
    int numerichosts;
    int tls;
    int blacklisthits;
} ClientOpts;

#endif

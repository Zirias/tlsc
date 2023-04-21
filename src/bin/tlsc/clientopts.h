#ifndef TLSC_CLIENTOPTS_H
#define TLSC_CLIENTOPTS_H

typedef enum ClientProto
{
    CP_ANY,
    CP_IPv4,
    CP_IPv6
} ClientProto;

typedef struct ClientOpts
{
    const char *remotehost;
    const char *tls_certfile;
    const char *tls_keyfile;
    ClientProto proto;
    int port;
    int numerichosts;
    int tls;
} ClientOpts;

#endif

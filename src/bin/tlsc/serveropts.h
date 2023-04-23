#ifndef TLSC_SERVEROPTS_H
#define TLSC_SERVEROPTS_H

#ifndef MAXBINDS
#define MAXBINDS 4
#endif

typedef enum ServerProto
{
    SP_ANY,
    SP_IPv4,
    SP_IPv6
} ServerProto;

typedef struct ServerOpts
{
    const char *bindhost[MAXBINDS];
    ServerProto proto;
    int port;
    int numerichosts;
    int connwait;
} ServerOpts;

#endif

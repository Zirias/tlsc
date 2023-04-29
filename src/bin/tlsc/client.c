#define _DEFAULT_SOURCE

#include "client.h"
#include "clientopts.h"
#include "connection.h"
#include "event.h"
#include "log.h"
#include "threadpool.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BLACKLISTSIZE 32
#define BLACKLISTHITS 3

typedef struct BlacklistEntry
{
    socklen_t len;
    struct sockaddr_storage val;
    int hits;
} BlacklistEntry;

typedef struct ResolveJobData
{
    struct addrinfo *res0;
    void *receiver;
    ClientCreatedHandler callback;
    ClientOpts opts;
} ResolveJobData;

static BlacklistEntry blacklist[BLACKLISTSIZE];

SOLOCAL void Connection_blacklistAddress(int hits,
	socklen_t len, struct sockaddr *addr)
{
    for (size_t i = 0; i < BLACKLISTSIZE; ++i)
    {
	if (blacklist[i].len) continue;
	memcpy(&blacklist[i].val, addr, len);
	blacklist[i].len = len;
	blacklist[i].hits = hits;
	return;
    }
}

static int blacklistcheck(socklen_t len, struct sockaddr *addr)
{
    for (size_t i = 0; i < BLACKLISTSIZE; ++i)
    {
	if (blacklist[i].len == len && !memcmp(&blacklist[i].val, addr, len))
	{
	    if (!--blacklist[i].hits) blacklist[i].len = 0;
	    return 0;
	}
    }
    return 1;
}

static Connection *createFromAddrinfo(const ClientOpts *opts,
	struct addrinfo *res0)
{
    struct addrinfo *res;
    int fd = -1;

    for (res = res0; res; res = res->ai_next)
    {
	if (res->ai_family != AF_INET && res->ai_family != AF_INET6) continue;
	if (opts->proto == CP_IPv4 && res->ai_family != AF_INET) continue;
	if (opts->proto == CP_IPv6 && res->ai_family != AF_INET6) continue;
	if (!blacklistcheck(res->ai_addrlen, res->ai_addr)) continue;
	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) continue;
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	errno = 0;
	if (connect(fd, res->ai_addr, res->ai_addrlen) < 0
		&& errno != EINPROGRESS)
	{
	    close(fd);
	    fd = -1;
	}
	else break;
    }
    if (fd < 0)
    {
	freeaddrinfo(res0);
	Log_fmt(L_ERROR, "client: cannot connect to `%s'", opts->remotehost);
	return 0;
    }
    ConnOpts copts = {
	.tls_client_certfile = opts->tls_certfile,
	.tls_client_keyfile = opts->tls_keyfile,
	.createmode = CCM_CONNECTING,
	.tls_client = opts->tls,
	.blacklisthits = opts->blacklisthits
    };
    Connection *conn = Connection_create(fd, &copts);
    Connection_setRemoteAddr(conn, res->ai_addr, res->ai_addrlen,
	    opts->numerichosts);
    freeaddrinfo(res0);
    return conn;
}

static struct addrinfo *resolveAddress(const ClientOpts *opts)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG|AI_NUMERICSERV;
    char portstr[6];
    snprintf(portstr, 6, "%d", opts->port);
    struct addrinfo *res0;
    if (getaddrinfo(opts->remotehost, portstr, &hints, &res0) < 0 || !res0)
    {
	Log_fmt(L_ERROR, "client: error resolving %s", opts->remotehost);
	return 0;
    }
    return res0;
}

static void doResolve(void *arg)
{
    ResolveJobData *data = arg;
    data->res0 = resolveAddress(&data->opts);
}

static void resolveDone(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;

    ResolveJobData *data = args;
    if (!data->res0) data->callback(data->receiver, 0);
    else data->callback(data->receiver,
	    createFromAddrinfo(&data->opts, data->res0));
    free(data);
}

SOLOCAL Connection *Connection_createTcpClient(const ClientOpts *opts)
{
    struct addrinfo *res0 = resolveAddress(opts);
    if (!res0) return 0;
    return createFromAddrinfo(opts, res0);
}

SOLOCAL int Connection_createTcpClientAsync(const ClientOpts *opts,
	void *receiver, ClientCreatedHandler callback)
{
    if (!ThreadPool_active())
    {
	Log_msg(L_ERROR, "client: async creation requires active ThreadPool");
	return -1;
    }

    ResolveJobData *data = xmalloc(sizeof *data);
    data->res0 = 0;
    data->receiver = receiver;
    data->callback = callback;
    memcpy(&data->opts, opts, sizeof *opts);
    ThreadJob *resolveJob = ThreadJob_create(doResolve, data, 0);
    Event_register(ThreadJob_finished(resolveJob), 0, resolveDone, 0);
    ThreadPool_enqueue(resolveJob);
    return 0;
}


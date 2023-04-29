#include "config.h"
#include "log.h"
#include "util.h"

#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#define ARGBUFSZ 16

#ifndef PIDFILE
#define PIDFILE "/var/run/tlsc.pid"
#endif

static const char *pidfile = PIDFILE;

struct Config
{
    TunnelConfig *tunnel;
    const char *pidfile;
    long uid;
    long gid;
    int daemonize;
    int numerichosts;
    int verbose;
};

struct TunnelConfig
{
    TunnelConfig *next;
    const char *bindhost;
    const char *remotehost;
    const char *certfile;
    const char *keyfile;
    int bindport;
    int remoteport;
    int blacklisthits;
    Proto serverproto;
    Proto clientproto;
};

static int addArg(char *args, int *idx, char opt)
    ATTR_NONNULL((1)) ATTR_NONNULL((2));
static int intArg(int *setting, char *op, int min, int max, int base, char **p)
    ATTR_NONNULL((1)) ATTR_NONNULL((2));
static int longArg(long *setting, char *op)
    ATTR_NONNULL((1)) ATTR_NONNULL((2));
static int optArg(Config *config, char *args, int *idx, char *op)
    ATTR_NONNULL((1)) ATTR_NONNULL((2)) ATTR_NONNULL((3)) ATTR_NONNULL((4));
static void usage(const char *prgname)
    ATTR_NONNULL((1));

static void usage(const char *prgname)
{
    fprintf(stderr,
	    "Usage: %s [-fnv] [-b hits] [-g group] [-p pidfile] [-u user]\n"
	    "       tunspec [tunspec ...]\n", prgname);
    fputs("\n\ttunspec        description of a tunnel in the format\n"
	    "\t               host:port:remotehost[:remoteport][:k=v[:...]]\n"
	    "\t               using these values:\n\n"
	    "\t\thost        hostname or IP address to bind to and listen\n"
	    "\t\tport        port to listen on\n"
	    "\t\tremotehost  remote host name to forward to with TLS\n"
	    "\t\tremoteport  port of remote service, default: same as `port`\n"
	    "\t\tk=v         key-value pair of additional tunnel options,\n"
	    "\t\t            the following are available:\n"
	    "\t\t  b=hits    a positive number enables blacklisting\n"
	    "\t\t            specific socket addresses for `hits`\n"
	    "\t\t            connection attempts after failure to connect\n"
	    "\t\t  c=cert    `cert` is used as a client certificate file to\n"
	    "\t\t            present to the remote\n"
	    "\t\t  k=key     `key` is the key file for the certificate\n"
	    "\t\t  p=[4|6]   only use IPv4 or IPv6\n"
	    "\t\t  pc=[4|6]  only use IPv4 or IPv6 when connecting as client\n"
	    "\t\t  ps=[4|6]  only use IPv4 or IPv6 when listening as server\n"
	    "\n"
	    "\t               Example:\n"
	    "\n"
	    "\t               \"localhost:12345:foo.example:443:b=2:pc=6\"\n"
	    "\n"
	    "\t               This will listen on localhost:12345 using any\n"
	    "\t               IP version available, and connect clients to\n"
	    "\t               foo.example:443 with TLS using only IPv6.\n"
	    "\t               Specific socket addresses of foo.example:443\n"
	    "\t               will be blacklisted for 2 hits after a\n"
	    "\t               connection error.\n"
	    "\n"
	    "\t-f             run in foreground, do not detach\n"
	    "\t-g group       group name/id to run as\n"
	    "\t               (defaults to primary group of user, see -u)\n"
	    "\t-n             use numeric hosts only, do not attempt\n"
	    "\t               to resolve addresses\n"
	    "\t-p pidfile     use `pidfile' instead of " PIDFILE "\n"
	    "\t-u user        user name/id to run as\n"
	    "\t               (defaults to current user)\n"
	    "\t-v             debug mode - will log [DEBUG] messages\n",
	    stderr);
}

static int addArg(char *args, int *idx, char opt)
{
    if (*idx == ARGBUFSZ) return -1;
    memmove(args+1, args, (*idx)++);
    args[0] = opt;
    return 0;
}

static int intArg(int *setting, char *op, int min, int max, int base, char **p)
{
    char *endp;
    errno = 0;
    long val = strtol(op, &endp, base);
    if (errno == ERANGE || (!p && *endp) || val < min || val > max) return -1;
    *setting = val;
    if (p) *p = endp;
    return 0;
}

static int longArg(long *setting, char *op)
{
    char *endp;
    errno = 0;
    long val = strtol(op, &endp, 10);
    if (errno == ERANGE || *endp) return -1;
    *setting = val;
    return 0;
}

static int optArg(Config *config, char *args, int *idx, char *op)
{
    if (!*idx) return -1;
    switch (args[--*idx])
    {
	case 'g':
	    if (longArg(&config->gid, op) < 0)
	    {
		struct group *g;
		if (!(g = getgrnam(op))) return -1;
		config->gid = g->gr_gid;
	    }
	    break;
	case 'p':
	    config->pidfile = op;
	    break;
	case 'u':
	    if (longArg(&config->uid, op) < 0)
	    {
		struct passwd *p;
		if (!(p = getpwnam(op))) return -1;
		config->uid = p->pw_uid;
		if (config->gid == -1)
		{
		    config->gid = p->pw_gid;
		}
	    }
	    break;
	default:
	    return -1;
    }
    return 0;
}

static char *tuntok(char *str, char sep)
{
    static char *s;
    if (str) s = str;
    if (!s || !*s) return 0;

    char *ret = s;
    int quot = 0;
    char *c;
    for (c = s; *c; ++c)
    {
	if (*c == '[' && ++quot) continue;
	if (*c == ']' && quot && --quot) continue;
	if (!quot && *c == sep)
	{
	    *c++ = 0;
	    break;
	}
    }
    s = c;
    return ret;
}

static char *tunhosttok(char *str)
{
    char *ret = tuntok(str, ':');
    if (!ret) return 0;
    if (*ret != '[') return ret;
    ++ret;
    ret[strcspn(ret, "]")] = 0;
    return ret;
}

static int tunkv(char *opt, char **k, char **v)
{
    size_t eqpos = strcspn(opt, "=");
    if (!opt[eqpos]) return -1;
    *k = opt;
    opt[eqpos] = 0;
    *v = opt+eqpos+1;
    return 0;
}

static TunnelConfig *parseTunnel(char *arg)
{
    char *bindhost = tunhosttok(arg);
    if (!bindhost) return 0;
    char *bindportstr = tuntok(0, ':');
    if (!bindportstr) return 0;
    char *remotehost = tunhosttok(0);
    if (!remotehost) return 0;
    int bindport;
    if (intArg(&bindport, bindportstr, 1, 65535, 10, 0) < 0) return 0;
    int remoteport = bindport;

    char *certfile = 0;
    char *keyfile = 0;
    int blacklisthits = 0;
    Proto serverproto = P_ANY;
    Proto clientproto = P_ANY;

    char *k;
    char *v;
    char *opt = tuntok(0, ':');
    if (opt)
    {
	if (tunkv(opt, &k, &v) < 0)
	{
	    if (intArg(&remoteport, opt, 1, 65535, 10, 0) < 0) return 0;
	    if ((opt = tuntok(0, ':')))
	    {
		if (tunkv(opt, &k, &v) < 0) return 0;
	    }
	    else goto optdone;
	}

	for (;;)
	{
	    if (!strcmp(k, "b"))
	    {
		if (intArg(&blacklisthits, v, 0, INT_MAX, 10, 0) < 0) return 0;
	    }
	    else if (!strcmp(k, "c")) certfile = v;
	    else if (!strcmp(k, "k")) keyfile = v;
	    else if (*k == 'p')
	    {
		Proto p = P_ANY;
		if (!strcmp(v, "4")) p = P_IPv4;
		else if (!strcmp(v, "6")) p = P_IPv6;
		else return 0;
		if (!k[1])
		{
		    serverproto = p;
		    clientproto = p;
		}
		else if (!strcmp(k, "pc")) clientproto = p;
		else if (!strcmp(k, "ps")) serverproto = p;
		else return 0;
	    }
	    else return 0;
	    if (!(opt = tuntok(0, ':'))) break;
	    if (tunkv(opt, &k, &v) < 0) return 0;
	}
optdone: ;
    }

    TunnelConfig *tun = xmalloc(sizeof *tun);
    tun->next = 0;
    tun->bindhost = bindhost;
    tun->remotehost = remotehost;
    tun->certfile = certfile;
    tun->keyfile = keyfile;
    tun->bindport = bindport;
    tun->remoteport = remoteport;
    tun->blacklisthits = blacklisthits;
    tun->serverproto = serverproto;
    tun->clientproto = clientproto;
    return tun;
}

SOLOCAL Config *Config_fromOpts(int argc, char **argv)
{
    int endflags = 0;
    int escapedash = 0;
    int needtun = 1;
    int arg;
    int naidx = 0;
    char needargs[ARGBUFSZ];
    const char onceflags[] = "fgnpuv";
    char seen[sizeof onceflags - 1] = {0};

    Config *config = xmalloc(sizeof *config);
    memset(config, 0, sizeof *config);
    config->pidfile = pidfile;
    config->daemonize = 1;

    const char *prgname = "tlsc";
    if (argc > 0) prgname = argv[0];

    for (arg = 1; arg < argc; ++arg)
    {
	char *o = argv[arg];
	if (!escapedash && *o == '-' && o[1] == '-' && !o[2])
	{
	    escapedash = 1;
	    continue;
	}

	if (!endflags && !escapedash && *o == '-' && o[1])
	{
	    if (naidx) goto error;

	    for (++o; *o; ++o)
	    {
		const char *sip = strchr(onceflags, *o);
		if (sip)
		{
		    int si = (int)(sip - onceflags);
		    if (seen[si])
		    {
			usage(prgname);
			goto error;
		    }
		    seen[si] = 1;
		}
		switch (*o)
		{
		    case 'f':
			config->daemonize = 0;
			break;

		    case 'n':
			config->numerichosts = 1;
			break;

		    case 'v':
			config->verbose = 1;
			break;

		    case 'g':
		    case 'p':
		    case 'u':
			if (addArg(needargs, &naidx, *o) < 0) goto silenterror;
			break;

		    default:
			if (optArg(config, needargs, &naidx, o) < 0) goto error;
			goto next;
		}
	    }
	}
	else if (optArg(config, needargs, &naidx, o) < 0)
	{
	    TunnelConfig *t = parseTunnel(o);
	    if (t)
	    {
		if (!config->tunnel) config->tunnel = t;
		else
		{
		    TunnelConfig *p = config->tunnel;
		    while (p->next) p = p->next;
		    p->next = t;
		}
		needtun = 0;
	    }
	    else goto error;
	    endflags = 1;
	}
next:	;
    }
    if (naidx || needtun) goto error;
    return config;

error:
    usage(prgname);
silenterror:
    Config_destroy(config);
    return 0;
}

SOLOCAL const TunnelConfig *Config_tunnel(const Config *self)
{
    return self->tunnel;
}

SOLOCAL const TunnelConfig *TunnelConfig_next(const TunnelConfig *self)
{
    return self->next;
}

SOLOCAL const char *TunnelConfig_bindhost(const TunnelConfig *self)
{
    return self->bindhost;
}

SOLOCAL const char *TunnelConfig_remotehost(const TunnelConfig *self)
{
    return self->remotehost;
}

SOLOCAL const char *TunnelConfig_certfile(const TunnelConfig *self)
{
    return self->certfile;
}

SOLOCAL const char *TunnelConfig_keyfile(const TunnelConfig *self)
{
    return self->keyfile;
}

SOLOCAL int TunnelConfig_bindport(const TunnelConfig *self)
{
    return self->bindport;
}

SOLOCAL int TunnelConfig_remoteport(const TunnelConfig *self)
{
    return self->remoteport;
}

SOLOCAL int TunnelConfig_blacklisthits(const TunnelConfig *self)
{
    return self->blacklisthits;
}

SOLOCAL Proto TunnelConfig_serverproto(const TunnelConfig *self)
{
    return self->serverproto;
}

SOLOCAL Proto TunnelConfig_clientproto(const TunnelConfig *self)
{
    return self->clientproto;
}

SOLOCAL const char *Config_pidfile(const Config *self)
{
    return self->pidfile;
}

SOLOCAL long Config_uid(const Config *self)
{
    return self->uid;
}

SOLOCAL long Config_gid(const Config *self)
{
    return self->gid;
}

SOLOCAL int Config_daemonize(const Config *self)
{
    return self->daemonize;
}

SOLOCAL int Config_numerichosts(const Config *self)
{
    return self->numerichosts;
}

SOLOCAL int Config_verbose(const Config *self)
{
    return self->verbose;
}

SOLOCAL void Config_destroy(Config *self)
{
    if (!self) return;
    TunnelConfig *t = self->tunnel;
    while (t)
    {
	TunnelConfig *n = t->next;
	free(t);
	t = n;
    }
    free(self);
}

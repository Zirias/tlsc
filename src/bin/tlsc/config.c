#include "config.h"
#include "log.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define ARGBUFSZ 16

static int addArg(char *args, int *idx, char opt)
    ATTR_NONNULL((1)) ATTR_NONNULL((2));
static int intArg(int *setting, char *op, int min, int max, int base, char **p)
    ATTR_NONNULL((1)) ATTR_NONNULL((2));
static int optArg(Config *config, char *args, int *idx, char *op)
    ATTR_NONNULL((1)) ATTR_NONNULL((2)) ATTR_NONNULL((3)) ATTR_NONNULL((4));
static void usage(const char *prgname)
    ATTR_NONNULL((1));

static void usage(const char *prgname)
{
    fprintf(stderr, "Usage: %s [-f] [-b address] [-p port] host port\n",
	    prgname);
    fputs("\n\t-b address     only bind to this address instead of any\n"
	    "\t-p port        listen on this port for connections\n"
	    "\t               (default: same as remote port)\n"
	    "\thost           remote host to connect to\n"
	    "\tport           TCP port to connect to\n\n",
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

static int optArg(Config *config, char *args, int *idx, char *op)
{
    if (!*idx) return -1;
    char *ep;
    switch (args[--*idx])
    {
	case 'b':
	    config->bindhost = op;
	    break;
	case 'p':
	    if (intArg(&config->bindport, op, 1, 65535, 10, &ep) < 0 || *ep)
	    {
		return -1;
	    }
	    break;
	default:
	    return -1;
    }
    return 0;
}

int Config_fromOpts(Config *config, int argc, char **argv)
{
    int endflags = 0;
    int escapedash = 0;
    int needhost = 1;
    int needport = 1;
    int arg;
    int naidx = 0;
    char needargs[ARGBUFSZ];
    const char onceflags[] = "bp";
    char seen[sizeof onceflags - 1] = {0};
    char *ep;

    memset(config, 0, sizeof *config);

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
	    if (naidx)
	    {
		usage(prgname);
		return -1;
	    }

	    for (++o; *o; ++o)
	    {
		const char *sip = strchr(onceflags, *o);
		if (sip)
		{
		    int si = (int)(sip - onceflags);
		    if (seen[si])
		    {
			usage(prgname);
			return -1;
		    }
		    seen[si] = 1;
		}
		switch (*o)
		{
		    case 'b':
		    case 'p':
			if (addArg(needargs, &naidx, *o) < 0) return -1;
			break;

		    default:
			if (optArg(config, needargs, &naidx, o) < 0)
			{
			    usage(prgname);
			    return -1;
			}
			goto next;
		}
	    }
	}
	else if (optArg(config, needargs, &naidx, o) < 0)
	{
	    if (needhost)
	    {
		config->remotehost = o;
		needhost = 0;
	    }
	    else if (needport)
	    {
		if (intArg(&config->remoteport, o, 1, 65535, 10, &ep) < 0
			|| *ep)
		{
		    usage(prgname);
		    return -1;
		}
		needport = 0;
	    }
	    else
	    {
		usage(prgname);
		return -1;
	    }
	    endflags = 1;
	}
next:	;
    }
    if (naidx || needhost || needport)
    {
	usage(prgname);
	return -1;
    }
    if (!config->bindport) config->bindport = config->remoteport;

    return 0;
}


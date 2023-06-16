#include "config.h"

#include <poser/core/proto.h>

#include <grp.h>
#include <pwd.h>
#include <stdlib.h>

#ifndef PIDFILE
#define PIDFILE "/var/run/tlsc.pid"
#endif

static void parseuser(PSC_ConfigParserCtx *ctx)
{
    struct passwd *p;
    if (PSC_ConfigParserCtx_succeeded(ctx))
    {
	long uid = PSC_ConfigParserCtx_integer(ctx);
	if (!(p = getpwuid(uid)))
	{
	    PSC_ConfigParserCtx_fail(ctx, "Unknown uid: %ld", uid);
	}
    }
    else
    {
	const char *user = PSC_ConfigParserCtx_string(ctx);
	if (!(p = getpwnam(user)))
	{
	    PSC_ConfigParserCtx_fail(ctx, "Unknown user: %s", user);
	}
	else PSC_ConfigParserCtx_setInteger(ctx, p->pw_uid);
    }
    if (p)
    {
	PSC_ConfigParserCtx_setIntegerFor(ctx, CFG_GROUP, p->pw_gid);
	PSC_ConfigParserCtx_succeed(ctx);
    }
}

static void parsegroup(PSC_ConfigParserCtx *ctx)
{
    struct group *g;
    if (PSC_ConfigParserCtx_succeeded(ctx))
    {
	long gid = PSC_ConfigParserCtx_integer(ctx);
	if (!(g = getgrgid(gid)))
	{
	    PSC_ConfigParserCtx_fail(ctx, "Unknown gid: %ld", gid);
	}
    }
    else
    {
	const char *group = PSC_ConfigParserCtx_string(ctx);
	if ((g = getgrnam(group)))
	{
	    PSC_ConfigParserCtx_setInteger(ctx, g->gr_gid);
	    PSC_ConfigParserCtx_succeed(ctx);
	}
	else PSC_ConfigParserCtx_fail(ctx, "Unknown group: %s", group);
    }
}

static void parseproto(PSC_ConfigParserCtx *ctx)
{
    if (!PSC_ConfigParserCtx_succeeded(ctx)) return;
    long protoarg = PSC_ConfigParserCtx_integer(ctx);
    if (protoarg == 4) PSC_ConfigParserCtx_setInteger(ctx, (long)PSC_P_IPv4);
    else if (protoarg == 6) PSC_ConfigParserCtx_setInteger(
	    ctx, (long)PSC_P_IPv6);
    else
    {
	PSC_ConfigParserCtx_fail(ctx,
		"Invalid protocol version: %ld", protoarg);
    }
}

static void parseanyproto(PSC_ConfigParserCtx *ctx)
{
    parseproto(ctx);
    if (!PSC_ConfigParserCtx_succeeded(ctx)) return;
    long proto = PSC_ConfigParserCtx_integer(ctx);
    PSC_ConfigParserCtx_setIntegerFor(ctx, CFG_T_SERVERPROTO, proto);
    PSC_ConfigParserCtx_setIntegerFor(ctx, CFG_T_CLIENTPROTO, proto);
}

static void parsetunport(PSC_ConfigParserCtx *ctx)
{
    if (!PSC_ConfigParserCtx_succeeded(ctx)) return;
    PSC_ConfigParserCtx_setIntegerFor(ctx, CFG_T_REMOTEPORT,
	    PSC_ConfigParserCtx_integer(ctx));
}

static void validateport(PSC_ConfigParserCtx *ctx)
{
    long port = PSC_ConfigParserCtx_integer(ctx);
    if (port < 1 || port > 65535)
    {
	PSC_ConfigParserCtx_fail(ctx, "Invalid port number: %ld", port);
    }
    else PSC_ConfigParserCtx_succeed(ctx);
}

static void validatetunnel(PSC_ConfigParserCtx *ctx)
{
    const PSC_Config *tuncfg = PSC_ConfigParserCtx_section(ctx);
    int isServer = !!PSC_Config_get(tuncfg, CFG_T_SERVER);
    int haveCert = !!PSC_Config_get(tuncfg, CFG_T_CERTFILE);
    int haveKey = !!PSC_Config_get(tuncfg, CFG_T_KEYFILE);

    if (haveCert && !haveKey) PSC_ConfigParserCtx_fail(ctx,
	    "A certificate file requires a key file");
    else if (haveKey && !haveCert) PSC_ConfigParserCtx_fail(ctx,
	    "A key file requires a cerrtificate file");
    else if (isServer && !haveCert) PSC_ConfigParserCtx_fail(ctx,
	    "A tunnel in server mode requires a certificate");
    else PSC_ConfigParserCtx_succeed(ctx);
}

SOLOCAL PSC_Config *Config_fromOpts(int argc, char **argv)
{
    PSC_ConfigElement *e;

    PSC_ConfigSection *root = PSC_ConfigSection_create(0);
    PSC_ConfigSection_addHelpArg(root, 0, 0, 0);
    PSC_ConfigSection_addVersionArg(root, "tlsc 2.0", 0, 0, 0);

    PSC_ConfigSection *tun = PSC_ConfigSection_create(CFG_TUNNEL);
    PSC_ConfigSection_validate(tun, validatetunnel);

    e = PSC_ConfigElement_createString(CFG_T_HOST, 0, 1);
    PSC_ConfigElement_argInfo(e, -1, 0);
    PSC_ConfigElement_describe(e,
	    "Hostname or IP address to bind to and listen on.");
    PSC_ConfigSection_add(tun, e);

    e = PSC_ConfigElement_createInteger(CFG_T_PORT, 0, 1);
    PSC_ConfigElement_argInfo(e, -1, 0);
    PSC_ConfigElement_describe(e, "Port to listen on.");
    PSC_ConfigElement_parse(e, parsetunport);
    PSC_ConfigElement_validate(e, validateport);
    PSC_ConfigSection_add(tun, e);

    e = PSC_ConfigElement_createString(CFG_T_REMOTEHOST, 0, 1);
    PSC_ConfigElement_argInfo(e, -1, 0);
    PSC_ConfigElement_describe(e,
	    "Remote hostname or IP address to forward to.");
    PSC_ConfigSection_add(tun, e);

    e = PSC_ConfigElement_createInteger(CFG_T_REMOTEPORT, 0, 0);
    PSC_ConfigElement_argInfo(e, -1, 0);
    PSC_ConfigElement_describe(e,
	    "Remote port to forward to, default: same as `port'.");
    PSC_ConfigElement_validate(e, validateport);
    PSC_ConfigSection_add(tun, e);

    e = PSC_ConfigElement_createInteger(CFG_T_BLACKLISTHITS, 0, 0);
    PSC_ConfigElement_argInfo(e, 'b', "hits");
    PSC_ConfigElement_describe(e, "A positive number enables blackisting "
	    "specific socket addresses for `hits' connection attempts "
	    "after failure to connect.");
    PSC_ConfigSection_add(tun, e);

    e = PSC_ConfigElement_createString(CFG_T_CERTFILE, 0, 0);
    PSC_ConfigElement_argInfo(e, 'c', "cert");
    PSC_ConfigElement_describe(e, "Use a certificate file to present to the "
	    "remote. When given, a key file is required as well.");
    PSC_ConfigSection_add(tun, e);

    e = PSC_ConfigElement_createString(CFG_T_KEYFILE, 0, 0);
    PSC_ConfigElement_argInfo(e, 'k', "key");
    PSC_ConfigElement_describe(e, "The key file for the certificate. "
	    "When given, a certificate must be configured as well.");
    PSC_ConfigSection_add(tun, e);

    e = PSC_ConfigElement_createInteger("proto", 0, 0);
    PSC_ConfigElement_argInfo(e, 'P', "[4|6]");
    PSC_ConfigElement_describe(e, "Only use IPv4 or IPv6.");
    PSC_ConfigElement_parse(e, parseanyproto);
    PSC_ConfigSection_add(tun, e);

    e = PSC_ConfigElement_createInteger(CFG_T_CLIENTPROTO, 0, 0);
    PSC_ConfigElement_argInfo(e, 'C', "[4|6]");
    PSC_ConfigElement_describe(e,
	    "Only use IPv4 or IPv6 when connection as a client.");
    PSC_ConfigElement_parse(e, parseproto);
    PSC_ConfigSection_add(tun, e);

    e = PSC_ConfigElement_createInteger(CFG_T_SERVERPROTO, 0, 0);
    PSC_ConfigElement_argInfo(e, 'S', "[4|6]");
    PSC_ConfigElement_describe(e,
	    "Only use IPv4 or IPv6 when listening as a server.");
    PSC_ConfigElement_parse(e, parseproto);
    PSC_ConfigSection_add(tun, e);

    e = PSC_ConfigElement_createBool(CFG_T_SERVER);
    PSC_ConfigElement_argInfo(e, 's', 0);
    PSC_ConfigElement_describe(e, "Enable or disable server mode (default: "
	    "disabled). In client mode, the forwarded connections use TLS. "
	    "In server mode, incoming connections use TLS.\n"
	    "When enabling server mode, a certificate is required.");
    PSC_ConfigSection_add(tun, e);

    e = PSC_ConfigElement_createBool(CFG_T_NOVERIFY);
    PSC_ConfigElement_argInfo(e, 'N', 0);
    PSC_ConfigElement_describe(e, "In client mode, enabling this option will "
	    "accept any server certificate. Otherwise (default), the server "
	    "certificate is validated agains the local store of trusted CAs.");
    PSC_ConfigSection_add(tun, e);

    e = PSC_ConfigElement_createSectionList(tun, 1);
    PSC_ConfigElement_argInfo(e, -1, "tunspec");
    PSC_ConfigElement_describe(e, "Description of a tunnel.");
    PSC_ConfigSection_add(root, e);

    e = PSC_ConfigElement_createBool(CFG_FOREGROUND);
    PSC_ConfigElement_argInfo(e, 'f', 0);
    PSC_ConfigElement_describe(e, "Run in foreground, do not detach.");
    PSC_ConfigSection_add(root, e);

    e = PSC_ConfigElement_createInteger(CFG_GROUP, -1, 0);
    PSC_ConfigElement_argInfo(e, 'g', "group");
    PSC_ConfigElement_parse(e, parsegroup);
    PSC_ConfigElement_describe(e, "Group name or id to run as. If a user "
	    "is given, this defaults to its primary group.");
    PSC_ConfigSection_add(root, e);

    e = PSC_ConfigElement_createBool(CFG_NUMERIC);
    PSC_ConfigElement_argInfo(e, 'n', 0);
    PSC_ConfigElement_describe(e, "Use numeric hosts only, do not attempt "
	    "to resolve addresses to hostnames.");
    PSC_ConfigSection_add(root, e);

    e = PSC_ConfigElement_createString(CFG_PIDFILE, PIDFILE, 0);
    PSC_ConfigElement_argInfo(e, 'p', "pidfile");
    PSC_ConfigElement_describe(e, "Use this pidfile instead of the default "
	    PIDFILE);
    PSC_ConfigSection_add(root, e);

    e = PSC_ConfigElement_createInteger(CFG_USER, -1, 0);
    PSC_ConfigElement_argInfo(e, 'u', "user");
    PSC_ConfigElement_parse(e, parseuser);
    PSC_ConfigElement_describe(e, "User name or id to run as.");
    PSC_ConfigSection_add(root, e);

    e = PSC_ConfigElement_createBool(CFG_VERBOSE);
    PSC_ConfigElement_argInfo(e, 'v', 0);
    PSC_ConfigElement_describe(e,
	    "Enable debug mode, will log debug messages.");
    PSC_ConfigSection_add(root, e);

    PSC_ConfigParser *parser = PSC_ConfigParser_create(root);
    PSC_ConfigParser_addArgs(parser, "tlsc", argc, argv);
    PSC_ConfigParser_argsAutoUsage(parser);
    PSC_ConfigParser_autoPage(parser);

    PSC_Config *cfg = 0;
    int rc = PSC_ConfigParser_parse(parser, &cfg);
    PSC_ConfigParser_destroy(parser);
    PSC_ConfigSection_destroy(root);

    if (rc < 0) exit(EXIT_FAILURE);
    else if (rc > 0) exit(EXIT_SUCCESS);

    return cfg;
}


#include "config.h"

#include <poser/core.h>

#include <stdlib.h>
#include <string.h>

#ifndef LOGIDENT
#define LOGIDENT "tlsc"
#endif

#define SERVCHUNK 16

typedef struct ServCtx
{
    PSC_Server *server;
    const TunnelConfig *tc;
} ServCtx;

typedef struct ConnCtx
{
    PSC_Connection *client;
    PSC_Connection *service;
    const char *chost;
    const char *shost;
    int connected;
} ConnCtx;

static const Config *cfg;
static ServCtx *servers = 0;
static size_t servcapa = 0;
static size_t servsize = 0;

static void datareceived(void *receiver, void *sender, void *args)
{
    (void)sender;

    PSC_Connection *c = receiver;
    PSC_EADataReceived_markHandling(args);
    PSC_Connection_write(c, PSC_EADataReceived_buf(args),
	    PSC_EADataReceived_size(args), args);
}

static void datasent(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Connection_confirmDataReceived(receiver);
}

static void logconnected(ConnCtx *ctx)
{
    if (ctx->chost && ctx->shost && ctx->connected)
    {
	PSC_Log_fmt(PSC_L_INFO, "Tlsc: connected %s:%d -> %s:%d",
		ctx->chost, PSC_Connection_remotePort(ctx->client),
		ctx->shost, PSC_Connection_remotePort(ctx->service));
    }
}

static void nameresolved(void *receiver, void *sender, void *args)
{
    (void)args;

    ConnCtx *ctx = receiver;
    PSC_Connection *c = sender;

    if (c == ctx->service) ctx->shost = PSC_Connection_remoteHost(c);
    else ctx->chost = PSC_Connection_remoteHost(c);

    logconnected(ctx);
}

static void connected(void *receiver, void *sender, void *args)
{
    (void)args;

    ConnCtx *ctx = receiver;
    PSC_Connection *sv = sender;
    PSC_Connection *cl = ctx->client;

    PSC_Event_register(PSC_Connection_dataReceived(cl), sv, datareceived, 0);
    PSC_Event_register(PSC_Connection_dataReceived(sv), cl, datareceived, 0);
    PSC_Event_register(PSC_Connection_dataSent(cl), sv, datasent, 0);
    PSC_Event_register(PSC_Connection_dataSent(sv), cl, datasent, 0);

    ctx->connected = 1;
    if (Config_numerichosts(cfg))
    {
	ctx->chost = PSC_Connection_remoteAddr(cl);
	ctx->shost = PSC_Connection_remoteAddr(sv);
    }
    logconnected(ctx);

    PSC_Connection_activate(cl);
}

static void connclosed(void *receiver, void *sender, void *args)
{
    ConnCtx *ctx = receiver;
    PSC_Connection *c = sender;
    PSC_Connection *o = (c == ctx->client) ? ctx->service : ctx->client;

    PSC_Event_unregister(PSC_Connection_closed(o), ctx, connclosed, 0);
    PSC_Event_unregister(PSC_Connection_closed(c), ctx, connclosed, 0);
    if (args)
    {
	PSC_Event_unregister(PSC_Connection_dataReceived(c), o,
		datareceived, 0);
	PSC_Event_unregister(PSC_Connection_dataReceived(o), c,
		datareceived, 0);
	PSC_Event_unregister(PSC_Connection_dataSent(c), o, datasent, 0);
	PSC_Event_unregister(PSC_Connection_dataSent(o), c, datasent, 0);
	const char *chost = PSC_Connection_remoteHost(c);
	if (!chost) chost = PSC_Connection_remoteAddr(c);
	const char *ohost = PSC_Connection_remoteHost(o);
	if (!ohost) ohost = PSC_Connection_remoteAddr(o);
	PSC_Log_fmt(PSC_L_INFO, "Tlsc: connection %s:%d <-> %s:%d closed",
		chost, PSC_Connection_remotePort(c),
		ohost, PSC_Connection_remotePort(o));
    }
    else
    {
	PSC_Event_unregister(PSC_Connection_connected(c), ctx, connected, 0);
    }

    PSC_Connection_close(o, 0);
    free(ctx);
}

static void svConnCreated(void *receiver, PSC_Connection *sv)
{
    ConnCtx *ctx = receiver;

    if (!sv)
    {
	PSC_Connection_close(ctx->client, 0);
	free(ctx);
	return;
    }

    ctx->service = sv;

    if (!Config_numerichosts(cfg))
    {
	PSC_Event_register(PSC_Connection_nameResolved(sv), ctx,
		nameresolved, 0);
    }
    PSC_Event_register(PSC_Connection_closed(ctx->client), ctx, connclosed, 0);
    PSC_Event_register(PSC_Connection_closed(sv), ctx, connclosed, 0);
    PSC_Event_register(PSC_Connection_connected(sv), ctx, connected, 0);
}

static void newclient(void *receiver, void *sender, void *args)
{
    (void)sender;

    ServCtx *ctx = receiver;
    PSC_Connection *cl = args;

    ConnCtx *cctx = PSC_malloc(sizeof *cctx);
    memset(cctx, 0, sizeof *cctx);
    cctx->client = cl;

    PSC_TcpClientOpts *opts = PSC_TcpClientOpts_create(
	    TunnelConfig_remotehost(ctx->tc),
	    TunnelConfig_remoteport(ctx->tc));
    PSC_TcpClientOpts_enableTls(opts, 
	    TunnelConfig_certfile(ctx->tc),
	    TunnelConfig_keyfile(ctx->tc));
    PSC_TcpClientOpts_setProto(opts,
	    TunnelConfig_clientproto(ctx->tc));
    PSC_TcpClientOpts_setBlacklistHits(opts,
	    TunnelConfig_blacklisthits(ctx->tc));
    if (Config_numerichosts(cfg)) PSC_TcpClientOpts_numericHosts(opts);
    if (TunnelConfig_noverify(ctx->tc))
	PSC_TcpClientOpts_disableCertVerify(opts);

    if (PSC_Connection_createTcpClientAsync(opts, cctx, svConnCreated) < 0)
    {
	PSC_Connection_close(cl, 0);
	free(cctx);
    }
    PSC_TcpClientOpts_destroy(opts);

    if (!Config_numerichosts(cfg))
    {
	PSC_Event_register(PSC_Connection_nameResolved(cl), cctx,
		nameresolved, 0);
    }
}

static void svprestartup(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;

    const TunnelConfig *tc = Config_tunnel(cfg);
    while (tc)
    {
	PSC_TcpServerOpts *opts = PSC_TcpServerOpts_create(
		TunnelConfig_bindport(tc));
	PSC_TcpServerOpts_bind(opts, TunnelConfig_bindhost(tc));
	PSC_TcpServerOpts_setProto(opts, TunnelConfig_serverproto(tc));
	PSC_TcpServerOpts_connWait(opts);
	if (Config_numerichosts(cfg)) PSC_TcpServerOpts_numericHosts(opts);
	PSC_Server *server = PSC_Server_createTcp(opts);
	PSC_TcpServerOpts_destroy(opts);
	if (!server)
	{
	    PSC_EAStartup_return(args, EXIT_FAILURE);
	    return;
	}
	if (servcapa == servsize)
	{
	    servcapa += SERVCHUNK;
	    servers = PSC_realloc(servers, servcapa * sizeof *servers);
	}
	servers[servsize].server = server;
	servers[servsize].tc = tc;
	PSC_Event_register(PSC_Server_clientConnected(server),
		&servers[servsize++], newclient, 0);
	tc = TunnelConfig_next(tc);
    }
}

static void svshutdown(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;
    (void)args;

    for (size_t i = 0; i < servsize; ++i)
    {
	PSC_Server_destroy(servers[i].server);
    }
    free(servers);
    servers = 0;
    servcapa = 0;
    servsize = 0;
}

SOLOCAL int Tlsc_run(const Config *config)
{
    cfg = config;

    PSC_RunOpts_init(Config_pidfile(cfg));
    PSC_RunOpts_runas(Config_uid(cfg), Config_gid(cfg));
    PSC_RunOpts_enableDefaultLogging(LOGIDENT);
    if (!Config_daemonize(cfg)) PSC_RunOpts_foreground();
    if (Config_verbose(cfg)) PSC_Log_setMaxLogLevel(PSC_L_DEBUG);

    PSC_ThreadOpts_init(8);
    PSC_ThreadOpts_maxThreads(16);

    PSC_Event_register(PSC_Service_prestartup(), 0, svprestartup, 0);
    PSC_Event_register(PSC_Service_shutdown(), 0, svshutdown, 0);

    return PSC_Service_run();
}


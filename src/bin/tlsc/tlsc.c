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
    const PSC_Config *tc;
} ServCtx;

typedef struct ConnCtx
{
    PSC_Connection *client;
    PSC_Connection *service;
    const char *chost;
    const char *shost;
    int connected;
} ConnCtx;

static const PSC_Config *cfg;
static ServCtx *servers = 0;
static size_t servcapa = 0;
static size_t servsize = 0;

static void datareceived(void *receiver, void *sender, void *args)
{
    (void)sender;

    PSC_Connection *c = receiver;
    PSC_EADataReceived_markHandling(args);
    PSC_Connection_sendAsync(c, PSC_EADataReceived_buf(args),
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
    if (PSC_Config_get(cfg, CFG_NUMERIC))
    {
	ctx->chost = PSC_Connection_remoteAddr(cl);
	ctx->shost = PSC_Connection_remoteAddr(sv);
    }
    logconnected(ctx);

    PSC_Connection_resume(cl);
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

    if (!PSC_Config_get(cfg, CFG_NUMERIC))
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

    PSC_Connection_pause(cl);

    ConnCtx *cctx = PSC_malloc(sizeof *cctx);
    memset(cctx, 0, sizeof *cctx);
    cctx->client = cl;

    PSC_TcpClientOpts *opts = PSC_TcpClientOpts_create(
	    PSC_Config_getString(ctx->tc, CFG_T_REMOTEHOST),
	    PSC_Config_getInteger(ctx->tc, CFG_T_REMOTEPORT));
    if (!PSC_Config_get(ctx->tc, CFG_T_SERVER))
    {
	PSC_TcpClientOpts_enableTls(opts,
		PSC_Config_getString(ctx->tc, CFG_T_CERTFILE),
		PSC_Config_getString(ctx->tc, CFG_T_KEYFILE));
    }
    PSC_TcpClientOpts_setProto(opts,
	    PSC_Config_getInteger(ctx->tc, CFG_T_CLIENTPROTO));
    PSC_TcpClientOpts_setBlacklistHits(opts,
	    PSC_Config_getInteger(ctx->tc, CFG_T_BLACKLISTHITS));
    if (PSC_Config_get(cfg, CFG_NUMERIC)) PSC_TcpClientOpts_numericHosts(opts);
    if (PSC_Config_get(ctx->tc, CFG_T_NOVERIFY))
	PSC_TcpClientOpts_disableCertVerify(opts);

    if (PSC_Connection_createTcpClientAsync(opts, cctx, svConnCreated) < 0)
    {
	PSC_Connection_close(cl, 0);
	free(cctx);
    }
    PSC_TcpClientOpts_destroy(opts);

    if (!PSC_Config_get(cfg, CFG_NUMERIC))
    {
	PSC_Event_register(PSC_Connection_nameResolved(cl), cctx,
		nameresolved, 0);
    }
}

static void svprestartup(void *receiver, void *sender, void *args)
{
    (void)receiver;
    (void)sender;

    PSC_ListIterator *i = PSC_List_iterator(PSC_Config_get(cfg, CFG_TUNNEL));
    while (PSC_ListIterator_moveNext(i))
    {
	const PSC_Config *tc = PSC_ListIterator_current(i);
	PSC_TcpServerOpts *opts = PSC_TcpServerOpts_create(
		PSC_Config_getInteger(tc, CFG_T_PORT));
	PSC_TcpServerOpts_bind(opts, PSC_Config_getString(tc, CFG_T_HOST));
	PSC_TcpServerOpts_setProto(opts,
		PSC_Config_getInteger(tc, CFG_T_SERVERPROTO));
	if (PSC_Config_get(tc, CFG_T_SERVER))
	{
	    PSC_TcpServerOpts_enableTls(opts,
		    PSC_Config_getString(tc, CFG_T_CERTFILE),
		    PSC_Config_getString(tc, CFG_T_KEYFILE));
	}
	if (PSC_Config_get(cfg, CFG_NUMERIC))
	{
	    PSC_TcpServerOpts_numericHosts(opts);
	}
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
    }
    PSC_ListIterator_destroy(i);
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

SOLOCAL int Tlsc_run(const PSC_Config *config)
{
    cfg = config;

    PSC_RunOpts_init(PSC_Config_getString(cfg, CFG_PIDFILE));
    PSC_RunOpts_runas(PSC_Config_getInteger(cfg, CFG_USER),
	    PSC_Config_getInteger(cfg, CFG_GROUP));
    PSC_RunOpts_enableDefaultLogging(LOGIDENT);
    if (PSC_Config_get(cfg, CFG_FOREGROUND)) PSC_RunOpts_foreground();
    if (PSC_Config_get(cfg, CFG_VERBOSE)) PSC_Log_setMaxLogLevel(PSC_L_DEBUG);

    PSC_ThreadOpts_init(8);
    PSC_ThreadOpts_maxThreads(16);

    PSC_Event_register(PSC_Service_prestartup(), 0, svprestartup, 0);
    PSC_Event_register(PSC_Service_shutdown(), 0, svshutdown, 0);

    return PSC_Service_run();
}


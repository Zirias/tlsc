#ifndef TLSC_CONFIG_H
#define TLSC_CONFIG_H

#include <poser/core/config.h>

#define CFG_TUNNEL	    "tunnel"
#define CFG_T_HOST	    "host"
#define CFG_T_REMOTEHOST    "remotehost"
#define CFG_T_CERTFILE	    "certfile"
#define CFG_T_KEYFILE	    "keyfile"
#define CFG_T_PORT	    "port"
#define CFG_T_REMOTEPORT    "remoteport"
#define CFG_T_BLACKLISTHITS "blacklisthits"
#define CFG_T_SERVER	    "server"
#define CFG_T_NOVERIFY	    "noverify"
#define CFG_T_SERVERPROTO   "serverproto"
#define CFG_T_CLIENTPROTO   "clientproto"
#define CFG_PIDFILE	    "pidfile"
#define CFG_USER	    "user"
#define CFG_GROUP	    "group"
#define CFG_FOREGROUND	    "foreground"
#define CFG_NUMERIC	    "numeric"
#define CFG_VERBOSE	    "verbose"

PSC_Config *Config_fromOpts(int argc, char **argv) ATTR_NONNULL((2));

#endif

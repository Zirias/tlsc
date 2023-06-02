#ifndef TLSC_CONFIG_H
#define TLSC_CONFIG_H

#include <poser/decl.h>
#include <poser/core/proto.h>

C_CLASS_DECL(Config);
C_CLASS_DECL(TunnelConfig);

Config *Config_fromOpts(int argc, char **argv) ATTR_NONNULL((2));
const TunnelConfig *Config_tunnel(const Config *self) CMETHOD ATTR_PURE;
const TunnelConfig *TunnelConfig_next(const TunnelConfig *self)
    CMETHOD ATTR_PURE;
const char *TunnelConfig_bindhost(const TunnelConfig *self) CMETHOD ATTR_PURE;
const char *TunnelConfig_remotehost(const TunnelConfig *self) CMETHOD ATTR_PURE;
const char *TunnelConfig_certfile(const TunnelConfig *self) CMETHOD ATTR_PURE;
const char *TunnelConfig_keyfile(const TunnelConfig *self) CMETHOD ATTR_PURE;
int TunnelConfig_bindport(const TunnelConfig *self) CMETHOD ATTR_PURE;
int TunnelConfig_remoteport(const TunnelConfig *self) CMETHOD ATTR_PURE;
int TunnelConfig_blacklisthits(const TunnelConfig *self) CMETHOD ATTR_PURE;
int TunnelConfig_server(const TunnelConfig *self) CMETHOD ATTR_PURE;
int TunnelConfig_noverify(const TunnelConfig *self) CMETHOD ATTR_PURE;
PSC_Proto TunnelConfig_serverproto(const TunnelConfig *self) CMETHOD ATTR_PURE;
PSC_Proto TunnelConfig_clientproto(const TunnelConfig *self) CMETHOD ATTR_PURE;
const char *Config_pidfile(const Config *self) CMETHOD ATTR_PURE;
long Config_uid(const Config *self) CMETHOD ATTR_PURE;
long Config_gid(const Config *self) CMETHOD ATTR_PURE;
int Config_daemonize(const Config *self) CMETHOD ATTR_PURE;
int Config_numerichosts(const Config *self) CMETHOD ATTR_PURE;
int Config_verbose(const Config *self) CMETHOD ATTR_PURE;
void Config_destroy(Config *self);

#endif

#ifndef TLSC_CONFIG_H
#define TLSC_CONFIG_H

#include "decl.h"

C_CLASS_DECL(Config);
C_CLASS_DECL(TunnelConfig);

Config *Config_fromOpts(int argc, char **argv) ATTR_NONNULL((2));
const TunnelConfig *Config_tunnel(const Config *self) CMETHOD;
const TunnelConfig *TunnelConfig_next(const TunnelConfig *self) CMETHOD;
const char *TunnelConfig_bindhost(const TunnelConfig *self) CMETHOD;
const char *TunnelConfig_remotehost(const TunnelConfig *self) CMETHOD;
const char *TunnelConfig_certfile(const TunnelConfig *self) CMETHOD;
const char *TunnelConfig_keyfile(const TunnelConfig *self) CMETHOD;
int TunnelConfig_bindport(const TunnelConfig *self) CMETHOD;
int TunnelConfig_remoteport(const TunnelConfig *self) CMETHOD;
const char *Config_pidfile(const Config *self) CMETHOD;
long Config_uid(const Config *self) CMETHOD;
long Config_gid(const Config *self) CMETHOD;
int Config_daemonize(const Config *self) CMETHOD;
int Config_numerichosts(const Config *self) CMETHOD;
int Config_verbose(const Config *self) CMETHOD;
void Config_destroy(Config *self);

#endif

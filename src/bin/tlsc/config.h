#ifndef TLSC_CONFIG_H
#define TLSC_CONFIG_H

#include "decl.h"

typedef struct Config
{
    const char *bindhost;
    const char *remotehost;
    int bindport;
    int remoteport;
    int daemonize;
} Config;

int Config_fromOpts(Config *config, int argc, char **argv)
    ATTR_NONNULL((1)) ATTR_NONNULL((3)) ATTR_ACCESS((write_only, 1));

#endif

#ifndef TLSC_DAEMON_H
#define TLSC_DAEMON_H

#include "decl.h"

typedef int (*daemon_main)(void *data);

int daemon_run(const daemon_main dmain, void *data,
	const char *pidfile, int waitLaunched) ATTR_NONNULL((1));
void daemon_launched(void);

#endif

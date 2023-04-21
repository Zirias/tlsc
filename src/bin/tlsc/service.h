#ifndef TLSC_SERVICE_H
#define TLSC_SERVICE_H

#include "decl.h"

#define MAXPANICHANDLERS 8

C_CLASS_DECL(DaemonOpts);
C_CLASS_DECL(Event);

typedef struct StartupEventArgs
{
    int rc;
} StartupEventArgs;

typedef void (*PanicHandler)(const char *msg) ATTR_NONNULL((1));

int Service_init(const DaemonOpts *options) ATTR_NONNULL((1));
Event *Service_readyRead(void) ATTR_RETNONNULL ATTR_PURE;
Event *Service_readyWrite(void) ATTR_RETNONNULL ATTR_PURE;
Event *Service_startup(void) ATTR_RETNONNULL ATTR_PURE;
Event *Service_shutdown(void) ATTR_RETNONNULL ATTR_PURE;
Event *Service_tick(void) ATTR_RETNONNULL ATTR_PURE;
Event *Service_eventsDone(void) ATTR_RETNONNULL ATTR_PURE;
void Service_registerRead(int id);
void Service_unregisterRead(int id);
void Service_registerWrite(int id);
void Service_unregisterWrite(int id);
void Service_registerPanic(PanicHandler handler) ATTR_NONNULL((1));
void Service_unregisterPanic(PanicHandler handler) ATTR_NONNULL((1));
int Service_setTickInterval(unsigned msec);
int Service_run(void);
void Service_quit(void);
void Service_shutdownLock(void);
void Service_shutdownUnlock(void);
void Service_panic(const char *msg) ATTR_NONNULL((1)) ATTR_NORETURN;
void Service_done(void);

#endif

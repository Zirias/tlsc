#ifndef TLSC_SERVER_H
#define TLSC_SERVER_H

#include "decl.h"

#include <stdint.h>

C_CLASS_DECL(Event);
C_CLASS_DECL(Server);
C_CLASS_DECL(ServerOpts);

Server *Server_createTcp(const ServerOpts *opts) ATTR_NONNULL((1));
Event *Server_clientConnected(Server *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;
Event *Server_clientDisconnected(Server *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;
void Server_destroy(Server *self);

#endif

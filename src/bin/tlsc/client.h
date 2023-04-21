#ifndef TLSC_CLIENT_H
#define TLSC_CLIENT_H

#include "decl.h"

C_CLASS_DECL(ClientOpts);
C_CLASS_DECL(Connection);

Connection *Connection_createTcpClient(const ClientOpts *opts)
    ATTR_NONNULL((1));

#endif

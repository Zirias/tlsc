#ifndef TLSC_CLIENT_H
#define TLSC_CLIENT_H

#include "decl.h"

C_CLASS_DECL(ClientOpts);
C_CLASS_DECL(Connection);

typedef void (*ClientCreatedHandler)(void *receiver, Connection *connection);

Connection *Connection_createTcpClient(const ClientOpts *opts)
    ATTR_NONNULL((1));
int Connection_createTcpClientAsync(const ClientOpts *opts,
	void *receiver, ClientCreatedHandler callback)
    ATTR_NONNULL((1)) ATTR_NONNULL((3));

#endif

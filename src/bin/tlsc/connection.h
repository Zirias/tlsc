#ifndef TLSC_CONNECTION_H
#define TLSC_CONNECTION_H

#include "decl.h"
#include "connopts.h"

#include <stdint.h>
#include <sys/socket.h>

C_CLASS_DECL(Connection);
C_CLASS_DECL(Event);

typedef struct DataReceivedEventArgs
{
    uint8_t *buf;
    int handling;
    uint16_t size;
} DataReceivedEventArgs;

Connection *Connection_create(int fd, const ConnOpts *opts)
    ATTR_RETNONNULL ATTR_NONNULL((2));
Event *Connection_connected(Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;
Event *Connection_closed(Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;
Event *Connection_dataReceived(Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;
Event *Connection_dataSent(Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;
Event *Connection_nameResolved(Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;
const char *Connection_remoteAddr(const Connection *self)
    CMETHOD ATTR_RETNONNULL ATTR_PURE;
const char *Connection_remoteHost(const Connection *self)
    CMETHOD ATTR_PURE;
void Connection_setRemoteAddr(Connection *self,
	struct sockaddr *addr, socklen_t addrlen, int numericOnly)
    CMETHOD ATTR_NONNULL((2));
void Connection_setRemoteAddrStr(Connection *self, const char *addr) CMETHOD;
int Connection_write(Connection *self,
	const uint8_t *buf, uint16_t sz, void *id) CMETHOD ATTR_NONNULL((2));
void Connection_activate(Connection *self) CMETHOD;
int Connection_confirmDataReceived(Connection *self) CMETHOD;
void Connection_close(Connection *self, int blacklist) CMETHOD;
void Connection_setData(Connection *self,
	void *data, void (*deleter)(void *)) CMETHOD;
void *Connection_data(const Connection *self) CMETHOD ATTR_PURE;
void Connection_destroy(Connection *self);

#endif

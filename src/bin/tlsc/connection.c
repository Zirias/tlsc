#define _DEFAULT_SOURCE

#include "client.h"
#include "connection.h"
#include "event.h"
#include "log.h"
#include "service.h"
#include "threadpool.h"
#include "util.h"

#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/ssl.h>

#define CONNBUFSZ 16*1024
#define NWRITERECS 16
#define CONNTICKS 6
#define RESOLVTICKS 6

static char hostbuf[NI_MAXHOST];
static char servbuf[NI_MAXSERV];

typedef struct WriteRecord
{
    const uint8_t *wrbuf;
    void *id;
    uint16_t wrbuflen;
    uint16_t wrbufpos;
} WriteRecord;

typedef struct RemoteAddrResolveArgs
{
    union {
	struct sockaddr sa;
	struct sockaddr_storage ss;
    };
    socklen_t addrlen;
    int rc;
    char name[NI_MAXHOST];
} RemoteAddrResolveArgs;

static SSL_CTX *tls_ctx = 0;
static int tls_nconn = 0;

typedef struct Connection
{
    Event *connected;
    Event *closed;
    Event *dataReceived;
    Event *dataSent;
    Event *nameResolved;
    ThreadJob *resolveJob;
    SSL *tls;
    char *addr;
    char *name;
    void *data;
    void (*deleter)(void *);
    WriteRecord writerecs[NWRITERECS];
    DataReceivedEventArgs args;
    RemoteAddrResolveArgs resolveArgs;
    int fd;
    int connecting;
    int port;
    int tls_connect_st;
    int tls_connect_ticks;
    int tls_read_st;
    int tls_write_st;
    uint8_t deleteScheduled;
    uint8_t nrecs;
    uint8_t baserecidx;
    uint8_t rdbuf[CONNBUFSZ];
} Connection;

void Connection_blacklistAddress(socklen_t len, struct sockaddr *addr)
    ATTR_NONNULL((2));

static void checkPendingConnection(void *receiver, void *sender, void *args);
static void wantreadwrite(Connection *self) CMETHOD;
static void checkPendingTls(void *receiver, void *sender, void *args);
static void dohandshake(Connection *self) CMETHOD;
static void dowrite(Connection *self) CMETHOD;
static void deleteConnection(void *receiver, void *sender, void *args);
static void deleteLater(Connection *self);
static void doread(Connection *self) CMETHOD;
static void readConnection(void *receiver, void *sender, void *args);
static void resolveRemoteAddrFinished(
	void *receiver, void *sender, void *args);
static void resolveRemoteAddrProc(void *arg);
static void writeConnection(void *receiver, void *sender, void *args);

static void checkPendingConnection(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    Connection *self = receiver;
    if (self->connecting && !--self->connecting)
    {
	Log_fmt(L_INFO, "connection: timeout connecting to %s",
		Connection_remoteAddr(self));
	Service_unregisterWrite(self->fd);
	Connection_close(self, 1);
    }
}

static void wantreadwrite(Connection *self)
{
    if (self->connecting ||
	    self->tls_connect_st == SSL_ERROR_WANT_WRITE ||
	    self->tls_read_st == SSL_ERROR_WANT_WRITE ||
	    self->tls_write_st == SSL_ERROR_WANT_WRITE ||
	    self->nrecs)
    {
	Service_registerWrite(self->fd);
    }
    else
    {
	Service_unregisterWrite(self->fd);
    }

    if (
	    self->tls_connect_st == SSL_ERROR_WANT_READ ||
	    self->tls_read_st == SSL_ERROR_WANT_READ ||
	    self->tls_write_st == SSL_ERROR_WANT_READ ||
	    !self->args.handling)
    {
	Service_registerRead(self->fd);
    }
    else
    {
	Service_unregisterRead(self->fd);
    }
}

static void checkPendingTls(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    Connection *self = receiver;
    if (self->tls_connect_ticks && !--self->tls_connect_ticks)
    {
	Log_fmt(L_INFO, "connection: TLS handshake timeout with %s",
		Connection_remoteAddr(self));
	Connection_close(self, 1);
    }
}

static void dohandshake(Connection *self)
{
    Log_fmt(L_DEBUG, "connection: handshake with %s",
	    Connection_remoteAddr(self));
    int rc = SSL_connect(self->tls);
    if (rc > 0)
    {
	self->tls_connect_st = 0;
	Log_fmt(L_DEBUG, "connection: connected to %s",
		Connection_remoteAddr(self));
	Event_unregister(Service_tick(), self, checkPendingTls, 0);
	self->tls_connect_ticks = 0;
	Event_raise(self->connected, 0, 0);
    }
    else
    {
	rc = SSL_get_error(self->tls, rc);
	if (rc == SSL_ERROR_WANT_READ || rc == SSL_ERROR_WANT_WRITE)
	{
	    self->tls_connect_st = rc;
	}
	else
	{
	    Log_fmt(L_ERROR, "connection: TLS handshake failed with %s",
		    Connection_remoteAddr(self));
	    Event_unregister(Service_tick(), self, checkPendingTls, 0);
	    Connection_close(self, 1);
	    return;
	}
    }
    wantreadwrite(self);
}

static void dowrite(Connection *self)
{
    Log_fmt(L_DEBUG, "connection: writing to %s",
	    Connection_remoteAddr(self));
    WriteRecord *rec = self->writerecs + self->baserecidx;
    void *id = 0;
    if (self->tls)
    {
	size_t writesz = 0;
	int rc = SSL_write_ex(self->tls, rec->wrbuf + rec->wrbufpos,
		rec->wrbuflen - rec->wrbufpos, &writesz);
	if (rc > 0)
	{
	    self->tls_write_st = 0;
	    if (writesz < rec->wrbuflen - rec->wrbufpos)
	    {
		rec->wrbufpos += writesz;
		wantreadwrite(self);
		return;
	    }
	    else id = rec->id;
	    if (++self->baserecidx == NWRITERECS) self->baserecidx = 0;
	    --self->nrecs;
	    if (id)
	    {
		Event_raise(self->dataSent, 0, id);
	    }
	}
	else
	{
	    rc = SSL_get_error(self->tls, rc);
	    if (rc == SSL_ERROR_WANT_READ || rc == SSL_ERROR_WANT_WRITE)
	    {
		self->tls_write_st = rc;
	    }
	    else
	    {
		Log_fmt(L_WARNING, "connection: error writing to %s",
			Connection_remoteAddr(self));
		Connection_close(self, 0);
		return;
	    }
	}
	wantreadwrite(self);
    }
    else
    {
	errno = 0;
	int rc = write(self->fd, rec->wrbuf + rec->wrbufpos,
		rec->wrbuflen - rec->wrbufpos);
	if (rc >= 0)
	{
	    if (rc < rec->wrbuflen - rec->wrbufpos)
	    {
		rec->wrbufpos += rc;
		return;
	    }
	    else id = rec->id;
	    if (++self->baserecidx == NWRITERECS) self->baserecidx = 0;
	    --self->nrecs;
	    wantreadwrite(self);
	    if (id)
	    {
		Event_raise(self->dataSent, 0, id);
	    }
	}
	else if (errno == EWOULDBLOCK || errno == EAGAIN)
	{
	    Log_fmt(L_INFO, "connection: not ready for writing to %s",
		    Connection_remoteAddr(self));
	    return;
	}
	else
	{
	    Log_fmt(L_WARNING, "connection: error writing to %s",
		    Connection_remoteAddr(self));
	    Connection_close(self, 0);
	}
    }
}

static void writeConnection(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    Connection *self = receiver;
    if (self->connecting)
    {
	Event_unregister(Service_tick(), self, checkPendingConnection, 0);
	int err = 0;
	socklen_t errlen = sizeof err;
	if (getsockopt(self->fd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0
		|| err)
	{
	    Log_fmt(L_INFO, "connection: failed to connect to %s",
		    Connection_remoteAddr(self));
	    Connection_close(self, 1);
	    return;
	}
	self->connecting = 0;
	if (self->tls)
	{
	    self->tls_connect_ticks = CONNTICKS;
	    Event_register(Service_tick(), self, checkPendingTls, 0);
	    dohandshake(self);
	    return;
	}
	wantreadwrite(self);
	Log_fmt(L_DEBUG, "connection: connected to %s",
		Connection_remoteAddr(self));
	Event_raise(self->connected, 0, 0);
	return;
    }
    Log_fmt(L_DEBUG, "connection: ready to write to %s",
	Connection_remoteAddr(self));
    if (self->tls_connect_st == SSL_ERROR_WANT_WRITE) dohandshake(self);
    else if (self->tls_read_st == SSL_ERROR_WANT_WRITE) doread(self);
    else
    {
	if (!self->nrecs)
	{
	    self->tls_write_st = 0;
	    Log_fmt(L_ERROR,
		    "connection: ready to send to %s with empty buffer",
		    Connection_remoteAddr(self));
	    wantreadwrite(self);
	    return;
	}
	dowrite(self);
    }
}

static void doread(Connection *self)
{
    Log_fmt(L_DEBUG, "connection: reading from %s",
	    Connection_remoteAddr(self));
    if (self->tls)
    {
	size_t readsz = 0;
	int ret = SSL_read_ex(self->tls, self->rdbuf, CONNBUFSZ, &readsz);
	if (ret > 0)
	{
	    self->tls_read_st = 0;
	    self->args.size = readsz;
	    Event_raise(self->dataReceived, 0, &self->args);
	    if (self->args.handling)
	    {
		Log_fmt(L_DEBUG, "connection: blocking reads from %s",
			Connection_remoteAddr(self));
	    }
	    else
	    {
		Log_fmt(L_DEBUG, "connection: done reading from %s",
			Connection_remoteAddr(self));
	    }
	}
	else
	{
	    int rc = SSL_get_error(self->tls, ret);
	    if (rc == SSL_ERROR_WANT_READ || rc == SSL_ERROR_WANT_WRITE)
	    {
		self->tls_read_st = rc;
		Log_fmt(L_DEBUG, "connection: reading from %s incomplete: %d",
			Connection_remoteAddr(self), rc);
	    }
	    else
	    {
		if (ret < 0)
		{
		    Log_fmt(L_WARNING, "connection: error reading from %s",
			    Connection_remoteAddr(self));
		}
		Connection_close(self, 0);
		return;
	    }
	}
	wantreadwrite(self);
    }
    else
    {
	errno = 0;
	int rc = read(self->fd, self->rdbuf, CONNBUFSZ);
	if (rc > 0)
	{
	    self->args.size = rc;
	    Event_raise(self->dataReceived, 0, &self->args);
	    if (self->args.handling)
	    {
		Log_fmt(L_DEBUG, "connection: blocking reads from %s",
			Connection_remoteAddr(self));
	    }
	    wantreadwrite(self);
	}
	else if (errno == EWOULDBLOCK || errno == EAGAIN)
	{
	    Log_fmt(L_INFO, "connection: ignoring spurious read from %s",
		    Connection_remoteAddr(self));
	}
	else
	{
	    if (rc < 0)
	    {
		Log_fmt(L_WARNING, "connection: error reading from %s",
			Connection_remoteAddr(self));
	    }
	    Connection_close(self, 0);
	}
    }
}

static void readConnection(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    Connection *self = receiver;
    Log_fmt(L_DEBUG, "connection: ready to read from %s",
	    Connection_remoteAddr(self));

    if (self->tls_connect_st == SSL_ERROR_WANT_READ) dohandshake(self);
    else if (self->tls_write_st == SSL_ERROR_WANT_READ) dowrite(self);
    else
    {
	if (self->args.handling)
	{
	    self->tls_read_st = 0;
	    Log_fmt(L_WARNING,
		    "connection: new data while read buffer from %s "
		    "still handled", Connection_remoteAddr(self));
	    wantreadwrite(self);
	    return;
	}
	doread(self);
    }
}

static void deleteConnection(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    Connection *self = receiver;
    self->deleteScheduled = 2;
    Connection_destroy(self);
}

SOLOCAL Connection *Connection_create(int fd, const ConnOpts *opts)
{
    Connection *self = xmalloc(sizeof *self);
    self->connected = Event_create(self);
    self->closed = Event_create(self);
    self->dataReceived = Event_create(self);
    self->dataSent = Event_create(self);
    self->nameResolved = Event_create(self);
    self->resolveJob = 0;
    self->fd = fd;
    self->connecting = 0;
    self->port = 0;
    self->addr = 0;
    self->name = 0;
    self->data = 0;
    self->deleter = 0;
    if (opts->tls_client)
    {
	if (!tls_ctx) tls_ctx = SSL_CTX_new(TLS_client_method());
	++tls_nconn;
	self->tls = SSL_new(tls_ctx);
	SSL_set_fd(self->tls, fd);
	if (opts->tls_client_certfile)
	{
	    if (opts->tls_client_keyfile)
	    {
		if (SSL_use_certificate_file(self->tls,
			    opts->tls_client_certfile, SSL_FILETYPE_PEM) > 0)
		{
		    if (SSL_use_PrivateKey_file(self->tls,
				opts->tls_client_keyfile,
				SSL_FILETYPE_PEM) <= 0)
		    {
			Log_fmt(L_ERROR, "connection: error loading private "
				"key %s.", opts->tls_client_keyfile);
		    }
		    else
		    {
			Log_fmt(L_INFO, "connection: using client "
				"certificate %s.", opts->tls_client_certfile);
		    }
		}
		else
		{
		    Log_fmt(L_ERROR, "connection: error loading "
			    "certificate %s.", opts->tls_client_certfile);
		}
	    }
	    else
	    {
		Log_msg(L_ERROR, "connection: certificate without private "
			"key, ignoring.");
	    }
	}
	else if (opts->tls_client_keyfile)
	{
	    Log_msg(L_ERROR, "connection: private key without certificate, "
		    "ignoring.");
	}
    }
    else
    {
	self->tls = 0;
    }
    self->tls_connect_st = 0;
    self->tls_read_st = 0;
    self->tls_write_st = 0;
    self->args.buf = self->rdbuf;
    self->args.handling = 0;
    self->deleteScheduled = 0;
    self->nrecs = 0;
    self->baserecidx = 0;
    Event_register(Service_readyRead(), self, readConnection, fd);
    Event_register(Service_readyWrite(), self, writeConnection, fd);
    if (opts->createmode == CCM_CONNECTING)
    {
	self->connecting = CONNTICKS;
	Event_register(Service_tick(), self, checkPendingConnection, 0);
	Service_registerWrite(fd);
    }
    else if (opts->createmode == CCM_NORMAL)
    {
	Service_registerRead(fd);
    }
    return self;
}

SOLOCAL Event *Connection_connected(Connection *self)
{
    return self->connected;
}

SOLOCAL Event *Connection_closed(Connection *self)
{
    return self->closed;
}

SOLOCAL Event *Connection_dataReceived(Connection *self)
{
    return self->dataReceived;
}

SOLOCAL Event *Connection_dataSent(Connection *self)
{
    return self->dataSent;
}

SOLOCAL Event *Connection_nameResolved(Connection *self)
{
    return self->nameResolved;
}

SOLOCAL const char *Connection_remoteAddr(const Connection *self)
{
    if (!self->addr) return "<unknown>";
    return self->addr;
}

SOLOCAL const char *Connection_remoteHost(const Connection *self)
{
    return self->name;
}

SOLOCAL int Connection_remotePort(const Connection *self)
{
    return self->port;
}

static void resolveRemoteAddrProc(void *arg)
{
    RemoteAddrResolveArgs *rara = arg;
    RemoteAddrResolveArgs tmp;
    memcpy(&tmp, rara, sizeof tmp);
    char buf[NI_MAXSERV];
    tmp.rc = getnameinfo(&tmp.sa, tmp.addrlen,
	    tmp.name, sizeof tmp.name, buf, sizeof buf, NI_NUMERICSERV);
    if (!ThreadJob_canceled()) memcpy(rara, &tmp, sizeof *rara);
}

static void resolveRemoteAddrFinished(void *receiver, void *sender, void *args)
{
    Connection *self = receiver;
    ThreadJob *job = sender;
    RemoteAddrResolveArgs *rara = args;

    if (ThreadJob_hasCompleted(job))
    {
	if (rara->rc >= 0 && strcmp(rara->name, self->addr) != 0)
	{
	    Log_fmt(L_DEBUG, "connection: %s is %s", self->addr, rara->name);
	    self->name = copystr(rara->name);
	}
	else
	{
	    Log_fmt(L_DEBUG, "connection: error resolving name for %s",
		    self->addr);
	}
    }
    else
    {
	Log_fmt(L_DEBUG, "connection: timeout resolving name for %s",
		self->addr);
    }
    self->resolveJob = 0;
    Event_raise(self->nameResolved, 0, 0);
}

SOLOCAL void Connection_setRemoteAddr(Connection *self,
	struct sockaddr *addr, socklen_t addrlen, int numericOnly)
{
    free(self->addr);
    free(self->name);
    self->addr = 0;
    self->name = 0;
    if (getnameinfo(addr, addrlen, hostbuf, sizeof hostbuf,
		servbuf, sizeof servbuf, NI_NUMERICHOST|NI_NUMERICSERV) >= 0)
    {
	self->addr = copystr(hostbuf);
	sscanf(servbuf, "%d", &self->port);
	if (!self->resolveJob)
	{
	    memcpy(&self->resolveArgs.sa, addr, addrlen);
	    self->resolveArgs.addrlen = addrlen;
	    if (!numericOnly && ThreadPool_active())
	    {
		self->resolveJob = ThreadJob_create(resolveRemoteAddrProc,
			&self->resolveArgs, RESOLVTICKS);
		Event_register(ThreadJob_finished(self->resolveJob), self,
			resolveRemoteAddrFinished, 0);
		ThreadPool_enqueue(self->resolveJob);
	    }
	    else Event_raise(self->nameResolved, 0, 0);
	}
    }
}

SOLOCAL void Connection_setRemoteAddrStr(Connection *self, const char *addr)
{
    free(self->addr);
    free(self->name);
    self->addr = copystr(addr);
    self->name = 0;
}

SOLOCAL int Connection_write(Connection *self,
	const uint8_t *buf, uint16_t sz, void *id)
{
    if (self->nrecs == NWRITERECS) return -1;
    WriteRecord *rec = self->writerecs +
	((self->baserecidx + self->nrecs++) % NWRITERECS);
    rec->wrbuflen = sz;
    rec->wrbufpos = 0;
    rec->wrbuf = buf;
    rec->id = id;
    wantreadwrite(self);
    return 0;
}

SOLOCAL void Connection_activate(Connection *self)
{
    if (self->args.handling) return;
    Log_fmt(L_DEBUG, "connection: unblocking reads from %s",
	    Connection_remoteAddr(self));
    wantreadwrite(self);
}

SOLOCAL int Connection_confirmDataReceived(Connection *self)
{
    if (!self->args.handling) return -1;
    self->args.handling = 0;
    Connection_activate(self);
    return 0;
}

SOLOCAL void Connection_close(Connection *self, int blacklist)
{
    if (self->tls && !self->connecting && !self->tls_connect_st)
    {
	SSL_shutdown(self->tls);
    }
    if (blacklist && self->resolveArgs.addrlen)
    {
	Connection_blacklistAddress(self->resolveArgs.addrlen,
		&self->resolveArgs.sa);
    }
    Event_raise(self->closed, 0, self->connecting ? 0 : self);
    deleteLater(self);
}

SOLOCAL void Connection_setData(Connection *self,
	void *data, void (*deleter)(void *))
{
    if (self->deleter) self->deleter(self->data);
    self->data = data;
    self->deleter = deleter;
}

SOLOCAL void *Connection_data(const Connection *self)
{
    return self->data;
}

static void cleanForDelete(Connection *self)
{
    Service_unregisterRead(self->fd);
    Service_unregisterWrite(self->fd);
    close(self->fd);
    if (self->resolveJob)
    {
	Event_unregister(ThreadJob_finished(self->resolveJob), self,
		resolveRemoteAddrFinished, 0);
	ThreadPool_cancel(self->resolveJob);
    }
}

static void deleteLater(Connection *self)
{
    if (!self) return;
    if (!self->deleteScheduled)
    {
	cleanForDelete(self);
	Event_register(Service_eventsDone(), self, deleteConnection, 0);
	self->deleteScheduled = 1;
    }
}

SOLOCAL void Connection_destroy(Connection *self)
{
    if (!self) return;
    if (self->deleteScheduled)
    {
	if (self->deleteScheduled == 1) return;
	Event_unregister(Service_eventsDone(), self, deleteConnection, 0);
    }
    else cleanForDelete(self);

    for (; self->nrecs; --self->nrecs)
    {
	WriteRecord *rec = self->writerecs + self->baserecidx;
	if (rec->id)
	{
	    Event_raise(self->dataSent, 0, rec->id);
	}
	if (++self->baserecidx == NWRITERECS) self->baserecidx = 0;
    }
    SSL_free(self->tls);
    if (tls_nconn && !--tls_nconn)
    {
	SSL_CTX_free(tls_ctx);
	tls_ctx = 0;
    }
    Event_unregister(Service_tick(), self, checkPendingTls, 0);
    Event_unregister(Service_tick(), self, checkPendingConnection, 0);
    Event_unregister(Service_readyRead(), self, readConnection, self->fd);
    Event_unregister(Service_readyWrite(), self, writeConnection, self->fd);
    if (self->deleter) self->deleter(self->data);
    free(self->addr);
    free(self->name);
    Event_destroy(self->nameResolved);
    Event_destroy(self->dataSent);
    Event_destroy(self->dataReceived);
    Event_destroy(self->closed);
    Event_destroy(self->connected);
    free(self);
}


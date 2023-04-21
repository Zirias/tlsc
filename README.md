# tlsc -- TLS connect daemon

A simple socket proxy for connecting to TLS-enabled services.

This daemon will listen on sockets (typically on `localhost`) and forward
connecting clients to some remote host, adding TLS encryption.

It does the job in the simplest possible way, using all-standard options.

This is currently work in progress. Client certificates will probably be
supported, `STARTTLS` in application protocols will *not* be supported.


# tlsc -- TLS connect daemon

A simple socket proxy for connecting non TLS-enabled clients to TLS-enabled
services or vice versa.

This daemon will listen on sockets and forward connecting clients to some
remote host, adding TLS encryption on either the client or the server side.

It does the job in the simplest possible way, using all-standard options.

## Usage
```
Usage: tlsc [-fnv] [-b hits] [-g group] [-p pidfile] [-u user]
       tunspec [tunspec ...]

	tunspec        description of a tunnel in the format
	               host:port:remotehost[:remoteport][:k=v[:...]]
	               using these values:

		host        hostname or IP address to bind to and listen
		port        port to listen on
		remotehost  remote host name to forward to
		remoteport  port of remote service, default: same as `port'
		k=v         key-value pair of additional tunnel options,
		            the following are available:
		  b=hits    a positive number enables blacklisting
		            specific socket addresses for `hits'
		            connection attempts after failure to connect
		  c=cert    `cert' is used as a certificate file to present
		            to the remote. When given, the `k' option is
		            required as well.
		  k=key     `key' is the key file for the certificate. When
		            given, the `c' option is required as well.
		  p=[4|6]   only use IPv4 or IPv6
		  pc=[4|6]  only use IPv4 or IPv6 when connecting as client
		  ps=[4|6]  only use IPv4 or IPv6 when listening as server
		  s=[0|1]   disable (0) or enable (1) server mode. In
		            client mode (default), the forwarded connection
		            uses TLS. In server mode, incoming connections
		            use TLS. When enabling server mode, the `c' and
		            `k' options are required to configure a
		            certificate.
		  v=[0|1]   disable (0) or enable (1) server certificate
		            verification (default: enabled)

	               Example:

	               "localhost:12345:foo.example:443:b=2:pc=6"

	               This will listen on localhost:12345 using any
	               IP version available, and connect clients to
	               foo.example:443 with TLS using only IPv6.
	               Specific socket addresses of foo.example:443
	               will be blacklisted for 2 hits after a
	               connection error.

	-f             run in foreground, do not detach
	-g group       group name/id to run as
	               (defaults to primary group of user, see -u)
	-n             use numeric hosts only, do not attempt
	               to resolve addresses
	-p pidfile     use `pidfile' instead of /var/run/tlsc.pid
	-u user        user name/id to run as
	               (defaults to current user)
	-v             debug mode - will log [DEBUG] messages
```

## Example

I currently use this tool myself to connect to an NNTP server with TLS like
this:

```
tlsc -u nobody localhost:8563:news.eternal-september.org:563
```


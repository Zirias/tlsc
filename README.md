# tlsc -- TLS connect daemon

A simple socket proxy for connecting to TLS-enabled services.

This daemon will listen on sockets (typically on `localhost`) and forward
connecting clients to some remote host, adding TLS encryption.

It does the job in the simplest possible way, using all-standard options.

## Usage
```
Usage: tlsc [-fnv] [-g group] [-p pidfile] [-u user]
       tunspec [tunspec ...]

        tunspec        description of a tunnel in the format
                       host:port:remotehost[:remoteport[:cert:key]]
                       using these values:

                host        hostname or IP address to bind to and listen
                port        port to listen on
                remotehost  remote host name to forward to with TLS
                remoteport  port of remote service, default: same as `port`
                cert        a certificate file to present to the remote
                key         the key file for the certificate

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


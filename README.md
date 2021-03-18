### Why?
Because the author thinks SSH (Shit Shell Hell) is a bloated piece of shit and RSH (Remote Shell) not being supported properly anywhere.
X11 forwarding, SFTP, blah blah. Thousands of features packed in of no use.

### What it does?
It listens on the machines default IP and a random free port (told in output of the program when run),
and goes into an infinite blocking [`accept(2)`][1] loop. On getting of a connection it spawns a subprocess
and redirects its stdin/stdout,stderr to the connection's socket.

### What's the difference?
The connection is unencrypted because it is intended for local network usage and it is unlikely that anyone
would attempt to intercept this connection, maybe except you?

### Usage
Run `./server /bin/bash` to run the server to serve a `bash` shell per connection. After `/bin/bash` can
follow multiple arguments with which the executable will be run.

[1]: https://man7.org/linux/man-pages/man2/accept.2.html

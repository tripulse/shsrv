### Why?
Becuase SSH goes past its intended function scope with features such as SFTP and X11 forwarding. This implicitly goes against the UNIX
philosophy of doing what a program is intended for, in other words composability over monolithicity and most of the times people tend
not to use these features at all.

Great drawback of it is not being able to disable encryption temporarily when in a isolated network where it is unlikely for a impostor
to peek in to see the *so-called **"confidential"*** information.

### What it does?
It uses a `AF_INET` socket with IP `0.0.0.0` and port `0` which translate to machine's default IP internally and a random free port
(output in the STDERR of the program) correspondingly, and enters into a [`accept(2)`][1] loop. Per successful connection establishment
a specified program is spawn as a subprocess whose input and output are bound to the slave-side of a PTY and master side of the PTY to
the socket connection, simulating a remote terminal.

### Usage
Run `./server /bin/bash` to run the server to serve a `bash` shell per connection. After `/bin/bash` can
follow multiple arguments with which the executable will be run. You may use `netcat` to act as the client
(flexibility, bam! Can your SSH do this?) like `netcat 0.0.0.0 8788`.

IP is obtained from `ip -4` and port is obtained from the output of `./server`.


[1]: https://man7.org/linux/man-pages/man2/accept.2.html

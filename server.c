#include <stdio.h>
#include <stdlib.h>

#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <sys/wait.h>
#include <sys/ioctl.h>

#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <pthread.h>

#define ERROUT(...) { \
    fprintf(stderr, __VA_ARGS__); \
    exit(EXIT_FAILURE); \
}

typedef struct {
    int          conn;    
    char *const *argv;
} request_ctx_t;

void* handle_request(request_ctx_t* ctx) {
    // open a controlling terminal for controlling the child process.
    int ptyma = posix_openpt(O_RDWR | O_NOCTTY);
    if(ptyma == -1)
        exit(EXIT_FAILURE);

    pid_t pid = fork();

    if(pid == 0) {
        // start a new session for job control, and remove off
        // the inherited controlling terminal.
        setsid();

        // open the slave side of the PTY (chowing, unlocking it)
        grantpt(ptyma);
        unlockpt(ptyma);

        int ttysl = open(ptsname(ptyma), O_RDWR);
        close(ptyma);

        // make our newly opened TTY the controlling terminal
        // and set the process of the new session
        ioctl(ttysl, TIOCSCTTY, 0);
        
        // make the stdio operations be done with the slave pty.
        dup2(ttysl, STDIN_FILENO);
        dup2(ttysl, STDOUT_FILENO);
        dup2(ttysl, STDERR_FILENO);

        execv(ctx->argv[3], ctx->argv + 4);
    } else if(pid == -1)
        exit(EXIT_FAILURE);

    int c2p[2], p2c[2];

    if(pipe2(c2p, O_NONBLOCK) != 0 || pipe2(p2c, O_NONBLOCK) != 0)
        exit(EXIT_FAILURE);

    fcntl(*c2p, F_SETPIPE_SZ, BUFSIZ);
    fcntl(*p2c, F_SETPIPE_SZ, BUFSIZ);

    // as here output is not dependant on input, for example to print
    // the PS1 from the /bin/bash program input is not required.
    // TODO: make it all be submitted in a event for less syscalls.
    fcntl(ctx->conn, F_SETFL, fcntl(ctx->conn, F_GETFL) | O_NONBLOCK);
    fcntl(ptyma,     F_SETFL, fcntl(ptyma, F_GETFL)     | O_NONBLOCK);

    struct pollfd connstat = {ctx->conn, POLLRDHUP};

    while(waitpid(pid, NULL, WNOHANG) == 0) {
        if(poll(&connstat, 1, 0) != 0 || connstat.revents & POLLRDHUP)
            break;

        splice(ctx->conn, NULL, c2p[1],    NULL, BUFSIZ, 0);
        splice(c2p[0],    NULL, ptyma,     NULL, BUFSIZ, 0);
        splice(ptyma,     NULL, p2c[1],    NULL, BUFSIZ, 0);
        splice(p2c[0],    NULL, ctx->conn, NULL, BUFSIZ, 0);
    }

    kill(pid, SIGTERM);    
    free(ctx);
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    if(argc < 4)
        ERROUT("usage: %s <ip> <port> <exec> [exec args...]", argv[0])

    static int errc;
    struct addrinfo *addrs;

    errc |= getaddrinfo(argv[1], argv[2], &(struct addrinfo){
        .ai_family    = AF_UNSPEC,
        .ai_socktype  = SOCK_STREAM,
        .ai_protocol  = IPPROTO_TCP,
        .ai_flags     = AI_NUMERICHOST | AI_NUMERICSERV,
        .ai_addrlen   = 0,
        .ai_addr      = NULL,
        .ai_canonname = NULL,
        .ai_next      = NULL
    }, &addrs);
    if(errc != 0)
        ERROUT("fail: network addr invalid: %s", gai_strerror(errc))

    int sock = socket(addrs->ai_family, SOCK_STREAM, IPPROTO_TCP);
    if(sock == -1)
        ERROUT("fatal: server sock alloc failed")
    
    errc  = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    errc |= setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int));
    errc |= setsockopt(sock, SOL_TCP,    SO_KEEPALIVE, &(int){1}, sizeof(int));

    if(errc != 0)
        fprintf(stderr, "warning: couldn't set sock opts");

    errc  = bind(sock, addrs->ai_addr, addrs->ai_addrlen);
    errc |= listen(sock, SOMAXCONN);
    if(errc != 0)
        ERROUT("fatal: sock binding failed")

    freeaddrinfo(addrs);

    // configuration for thread creation for serving each request.
    pthread_attr_t thattr;

    pthread_attr_init(&thattr);
    pthread_attr_setdetachstate(&thattr, PTHREAD_CREATE_DETACHED);

    for(;;) {
        struct sockaddr_storage sa;
        socklen_t               salen;

        int conn = accept(sock, (struct sockaddr*)&sa, &salen);
        if(conn == -1)
            continue;
        
        request_ctx_t* ctx = malloc(sizeof(request_ctx_t));

        ctx->argv = argv;
        ctx->conn = conn;

        pthread_create(&(pthread_t){0}, &thattr, handle_request, ctx);
    }
}
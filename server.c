#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <err.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if(argc < 2)
        errx(EXIT_FAILURE, "fatal: no executable specified");

    static int errc;
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = 0,
        .sin_zero   = {0},
        .sin_addr   = {0}
    };

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(sock == -1)
        errx(EXIT_FAILURE, "fatal: socket allocation failed");
    
    errc  = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    errc |= setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int));
    errc |= setsockopt(sock, SOL_TCP,    SO_KEEPALIVE, &(int){1}, sizeof(int));

    if(errc != 0)
        warnx("warning: couldn't set socket options");

    errc  = bind(sock, (struct sockaddr*)&addr, (socklen_t)sizeof addr);
    errc |= listen(sock, SOMAXCONN);

    if(errc != 0)
        err(EXIT_FAILURE, "fatal: couldn't initialise the socket");

    getsockname(sock, (struct sockaddr*)&addr, &(socklen_t){sizeof addr});
    
    fprintf(stderr, "info: listening on 0.0.0.0:%d\n", ntohs(addr.sin_port));

    for(;;) {
        int conn = accept(sock, NULL, NULL);
        if(conn == -1)
            continue;

        pid_t pid = fork();

        if(pid == 0) {
            dup2(conn, STDIN_FILENO);
            dup2(conn, STDOUT_FILENO);
            dup2(conn, STDERR_FILENO);

            execv(argv[1], argv + 2);
        }
    }
}
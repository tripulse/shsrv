#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <unistd.h>
#include <arpa/inet.h>

#include <sys/ioctl.h>
#include <poll.h>
#include <liburing.h>

#include <fcntl.h>

int main(int argc, char** argv) {
    assert(argc >= 3);

    struct sockaddr_in addr;

    assert(inet_pton(AF_INET, argv[1], &addr.sin_addr));
    assert(sscanf(argv[2], "%hu", &addr.sin_port));
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(addr.sin_port);

    inet_ntop(AF_INET, &addr.sin_addr, argv[1], INET_ADDRSTRLEN);

    fprintf(stderr, "%s:%hu\n", argv[1], ntohs(addr.sin_port));

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    assert(connect(sock, &addr, sizeof addr) == 0);

    // struct {
    //     uint8_t  tty;
    //     uint16_t cols;
    //     uint16_t rows;
    // } meta = { isatty(1) };
    // ioctl(1, TIOCGWINSZ, (&meta)+1);

    // assert(write(sock, &meta, 5) == 5);

    int c2s[2], s2c[2];

    assert(pipe(c2s) == 0 && pipe(s2c) == 0);
    assert(fcntl(*c2s, F_SETPIPE_SZ, BUFSIZ) == BUFSIZ);
    assert(fcntl(*s2c, F_SETPIPE_SZ, BUFSIZ) == BUFSIZ);

    struct io_uring ring;

    io_uring_queue_init(5, &ring, 0);

    for(;;) {
        struct io_uring_sqe* sqe;
    
        #define io_uring_splice_AS(...) \
            sqe = io_uring_get_sqe(&ring); \
            io_uring_prep_splice(sqe, __VA_ARGS__)

        io_uring_splice_AS(0,      0, c2s[1], 0, BUFSIZ, SPLICE_F_MOVE);
        io_uring_splice_AS(c2s[0], 0, sock,   0, BUFSIZ, SPLICE_F_MOVE | SPLICE_F_MORE);
        io_uring_splice_AS(sock,   0, s2c[1], 0, BUFSIZ, SPLICE_F_MOVE);
        io_uring_splice_AS(s2c[0], 0, 1,      0, BUFSIZ, SPLICE_F_MOVE);

        // sqe = io_uring_get_sqe(&ring);
        // io_uring_prep_poll_add(sqe, sock, POLLRDHUP);
        // io_uring_sqe_set_data(sqe, &sock);

        io_uring_submit(&ring);

        for(struct io_uring_cqe* cqe; io_uring_peek_cqe(&ring, &cqe) == 0;) {
            // if(io_uring_cqe_get_data(cqe) != NULL)
            //     break;
            
            io_uring_cqe_seen(&ring, cqe);
        }
    }

    io_uring_queue_exit(&ring);
}
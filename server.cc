#include <stdio.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <pty.h>
#include <utmp.h>

#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <err.h>
#include <pthread.h>

#include <liburing.h>

#include <vector>
#include <mutex>
#include <tuple>
#include <thread>

/**
 * This class is instantiated in a different thread just for the sake of copying
 * the data between the socket and the master end of the PTY, and periodically
 * check for the slave TTYs connection and socket's connection hangups.
 */
class Sock2TTYBridge {
private:
    typedef std::pair<int, int> pipe_t;

    ///                         SOCK            RW(S->T)RW(T->S) TTY
    std::vector<std::tuple<int, std::pair<pipe_t, pipe_t>, int>> conns;
    std::mutex mtx;
public:
    size_t fdcopy_pipesz = BUFSIZ;

    bool append(int conn, int tty) {
        mtx.lock();

        int s2t[2], t2s[2];

        if(pipe(s2t) != 0 || pipe(t2s) != 0) {
            close(conn);
            close(tty);

            return false;
        }

        if(fcntl(s2t[0], F_SETPIPE_SZ, fdcopy_pipesz) != 0 ||
           fcntl(t2s[0], F_SETPIPE_SZ, fdcopy_pipesz) != 0)
           warnx("warning: couldn't set pipe buffer size to %zu", fdcopy_pipesz);

        conns.emplace_back(conn,
            std::make_pair(
                std::make_pair(s2t[0], s2t[1]),
                std::make_pair(t2s[0], t2s[1])), tty);
        mtx.unlock();

        return true;
    }

    static void* run(void* this__) {
        auto this_ = static_cast<Sock2TTYBridge*>(this__);

        io_uring ring;

        io_uring_queue_init(6, &ring, 0);

        for(;;) {
            this_->mtx.lock();

            size_t conn_idx = 0;
            for(auto conn = this_->conns.begin(); conn != this_->conns.end(); ++conn) {
                struct io_uring_sqe* sqe;

                #define io_uring_splice_AS(...) \
                    sqe = io_uring_get_sqe(&ring); \
                    io_uring_prep_splice(sqe, __VA_ARGS__)

                #define io_uring_hup_AS(fd) \
                    sqe = io_uring_get_sqe(&ring); \
                    io_uring_prep_poll_add(sqe, fd, POLLRDHUP); \
                    io_uring_sqe_set_data(sqe, &conn_idx)

                io_uring_splice_AS(std::get<0>(*conn), 0, std::get<1>(*conn).first.second, 0, BUFSIZ, 0);
                io_uring_splice_AS(std::get<1>(*conn).first.first, 0, std::get<2>(*conn), 0, BUFSIZ, 0);
                io_uring_splice_AS(std::get<2>(*conn), 0, std::get<1>(*conn).second.second, 0, BUFSIZ, 0);
                io_uring_splice_AS(std::get<1>(*conn).second.first, 0, std::get<0>(*conn), 0, BUFSIZ, 0);

                io_uring_hup_AS(std::get<0>(*conn));
                io_uring_hup_AS(std::get<2>(*conn));

                io_uring_submit(&ring);

                ++conn_idx;
            }
            
            io_uring_cqe* cqe;

            while(io_uring_peek_cqe(&ring, &cqe) == 0) {
                auto conn_idx = static_cast<size_t*>(io_uring_cqe_get_data(cqe));

                if(conn_idx == NULL)
                    continue;

                auto conn = this_->conns[*conn_idx];

                close(std::get<0>(conn));
                close(std::get<1>(conn).first.first);
                close(std::get<1>(conn).first.second);
                close(std::get<1>(conn).second.first);
                close(std::get<1>(conn).second.second);
                close(std::get<2>(conn));

                // remove it from the queue as will not be used anymore.
                this_->conns[*conn_idx] = this_->conns.back();
                this_->conns.pop_back();

                io_uring_cqe_seen(&ring, cqe);
            }

            this_->mtx.unlock();
        }
        
        io_uring_queue_exit(&ring);
    }
};

int main(int argc, char** argv) {
    if(argc < 2)
        errx(EXIT_FAILURE, "fatal: no executable specified");

    static int errc;
    struct sockaddr_in addr = { AF_INET, 0, {0}, {0} };

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(sock == -1)
        errx(EXIT_FAILURE, "fatal: socket allocation failed");
    
    int yes_sockopt = 1;

    errc  = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes_sockopt, sizeof(int));
    errc |= setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &yes_sockopt, sizeof(int));
    errc |= setsockopt(sock, SOL_TCP,    SO_KEEPALIVE, &yes_sockopt, sizeof(int));

    if(errc != 0)
        warnx("warning: couldn't set socket options");

    errc  = bind(sock, (struct sockaddr*)&addr, (socklen_t)sizeof addr);
    errc |= listen(sock, SOMAXCONN);

    if(errc != 0)
        err(EXIT_FAILURE, "fatal: couldn't initialise the socket");

    socklen_t slen = sizeof addr;

    getsockname(sock, (struct sockaddr*)&addr, &slen);
    fprintf(stderr, "info: listening on 0.0.0.0:%d\n", ntohs(addr.sin_port));

    // accept(3) thread adds in file descriptor set to copy,
    // and a dedicated thread runs its .run() method.
    Sock2TTYBridge fdcopy;

    {
        pthread_t fdcopy_thread;
        pthread_attr_t fdcopy_thattr;

        pthread_attr_init(&fdcopy_thattr);
        pthread_attr_setdetachstate(&fdcopy_thattr, PTHREAD_CREATE_DETACHED);
        pthread_create(&fdcopy_thread, &fdcopy_thattr, &fdcopy.run, &fdcopy);
    }

    for(;;) {
        int conn = accept(sock, NULL, NULL);
        if(conn == -1)
            continue;

        struct {
            uint8_t  is;
            uint16_t cols, rows;
        } ttyprops;

        if(read(conn, &ttyprops, 5) < 5) {
            close(conn);  // the connection died off before it
            continue;     // could sent the metadata properly.
        }
        
        int ttyma, ttysl;

        if(ttyprops.is) {
            struct winsize ttywinsz = {
                .ws_row = ntohs(ttyprops.rows),
                .ws_col = ntohs(ttyprops.cols)
            };

            if(openpty(&ttyma, &ttysl, NULL, NULL, &ttywinsz) == 0)
                continue;

            fcntl(conn,  F_SETFL, fcntl(conn,  F_GETFL) | O_CLOEXEC);
            fcntl(ttyma, F_SETFL, fcntl(ttyma, F_GETFL) | O_CLOEXEC);
        }

        pid_t pid = fork();

        if(pid == 0) {
            if(ttyprops.is)
                login_tty(ttysl);
            else {
                dup2(STDIN_FILENO,  conn);
                dup2(STDOUT_FILENO, conn);
                dup2(STDERR_FILENO, conn);
            }

            execv(argv[1], argv + 2);
        }

        close(ttysl);

        if(ttyprops.is)
            fdcopy.append(conn, ttyma);
    }
}
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>
#include <getopt.h>
#include "log.h"

#define DEFAULT_SERVER_PORT (9998)
#define DEFAULT_REMOTE_PORT (9999)
#define DEFAULT_SERVER_ADDR "10.0.2.15"
#define DEFAULT_REMOTE_ADDR "10.0.2.15"
#define PROXY_MODE_NORMAL (1)
#define PROXY_MODE_SPLICE (2)

static struct test_options {
    int verbose;
    int server_port;
    int remote_port;
    int mode;
    char server_addr[40];
    char remote_addr[40];
} test_options = {
    .verbose = 0,
    .server_port = DEFAULT_SERVER_PORT,
    .remote_port = DEFAULT_REMOTE_PORT,
    .mode = PROXY_MODE_NORMAL,
    .server_addr = DEFAULT_SERVER_ADDR,
    .remote_addr = DEFAULT_REMOTE_ADDR,
};

struct ctxt  {
    int sock_pair[2];
    const char *endpoint;
};

static int do_copy_splice(int r_fd, int w_fd, char *buf, size_t bufsize, int pipe_fd[2]) {
    int nbytes = splice(r_fd, NULL, pipe_fd[1], NULL, bufsize, SPLICE_F_NONBLOCK | SPLICE_F_MOVE);
    if(nbytes <= 0) {
        if(nbytes < 0) {
            //if no data or sigint, retry
            if(errno == EAGAIN || errno == EINTR) {
                return 1;
            }
            perror("splice:");
        }
        return 0;
    }
    nbytes = splice(pipe_fd[0], NULL, w_fd, NULL, nbytes, SPLICE_F_NONBLOCK | SPLICE_F_MOVE);
    if(nbytes <= 0) {
        if(nbytes < 0) {
            if(errno == EAGAIN || errno == EINTR) {
                return 1;
            }
            perror("splice out:");
        }
        return 0;
    }
    return nbytes;
}

static int do_copy(int r_fd, int w_fd, char *buf, size_t bufsize, int unused_pipe[2]) {
    int nbytes, wbytes;
    (void)unused_pipe;
    nbytes = read(r_fd, buf, bufsize);
    if(nbytes <= 0) {
        if(errno == EINTR) {
            return 1;
        }
        return 0;
    }
    if( ( wbytes = write(w_fd, buf, nbytes) ) != nbytes) {
        perror("write:");
        nbytes = wbytes;
    }
    return nbytes;
}

static void *proxy_packet(void *arg) {
    struct ctxt *ctx = arg;
    int r_fd = ctx->sock_pair[0], w_fd = ctx->sock_pair[1];
    int mode = test_options.mode;
    int nbytes;
    int err = -1;
    char buf[65536];
    static size_t bufsize = sizeof(buf);
    char *buf_p = buf;
    int pipe_fds[2];
    int *pipe_p = NULL;
    int (*copy_cb)(int r_fd, int w_fd, char *buf, size_t bufsize, int pipe_fd[2]) = do_copy;
    if ( (mode & PROXY_MODE_SPLICE) ) {
        buf_p = NULL;
        if(pipe(pipe_fds) < 0) {
            perror("pipe splice:");
            goto out_close;
        }
        assert(fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK | O_CLOEXEC) == 0);
        assert(fcntl(pipe_fds[1], F_SETFL, O_NONBLOCK | O_CLOEXEC) == 0);
        pipe_p = pipe_fds;
        copy_cb = do_copy_splice;
    }
    for(;;) {
        nbytes = copy_cb(r_fd, w_fd, buf_p, bufsize, pipe_p);
        if(nbytes <= 0) {
            err = nbytes;
            goto out_close;
        }
    }
    err = 0;
    out_close:
    close(r_fd);
    close(w_fd);
    return (void*)(long)err;
}

static int do_proxy(const char *host, int port, int r_fd) {
    pthread_t tids[2];
    struct ctxt ctx[2];
    int i;
    int w_fd;
    int err = -1;
    int en = 1;
    struct sockaddr_in caddr;
    int rcvbuf = 1<<20;
    int sndbuf = 1<<20;
    memset(&caddr, 0, sizeof(caddr));
    caddr.sin_addr.s_addr = inet_addr(host);
    caddr.sin_port = htons(port);
    caddr.sin_family = PF_INET;
    w_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(w_fd >= 0);
    assert(setsockopt(w_fd, SOL_TCP, TCP_NODELAY, &en, sizeof(en)) == 0);

    assert(setsockopt(w_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) == 0);
    assert(setsockopt(w_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) == 0);
    assert(setsockopt(r_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) == 0);
    assert(setsockopt(r_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) == 0);
    en = 1;
    assert(setsockopt(r_fd, SOL_TCP, TCP_NODELAY, &en, sizeof(en)) == 0);

    if(connect(w_fd, (struct sockaddr*)&caddr, sizeof(caddr)) < 0) {
        perror("bind:");
        goto out_close;
    }
    //connect success, we can start the comm/proxy
    ctx[0].sock_pair[0] = r_fd;
    ctx[0].sock_pair[1] = w_fd;
    ctx[0].endpoint = "proxy-REMOTE";
    assert(pthread_create(&tids[0], NULL, proxy_packet, &ctx[0]) == 0);
    ctx[1].sock_pair[0] = w_fd;
    ctx[1].sock_pair[1] = r_fd;
    ctx[1].endpoint = "REMOTE-proxy";
    assert(pthread_create(&tids[1], NULL, proxy_packet, &ctx[1]) == 0);
    for(i = 0; i < sizeof(tids)/sizeof(tids[0]); ++i) {
        pthread_join(tids[i], NULL);
    }
    err = 0;
    out_close:
    close(r_fd);
    close(w_fd);
    return err;
}

static int do_server(const char *server_addr, const int port,
                     const char *proxy_addr, const int proxy_port) {
    struct sockaddr_in server;
    int fd,sd,en=1;
    int err = -1;
    memset(&server, 0, sizeof(server));
    server.sin_family = PF_INET;
    server.sin_addr.s_addr = inet_addr(server_addr);
    server.sin_port = htons(port);
    if( (fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket:");
        goto out;
    }
    assert(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &en, sizeof(en)) == 0);
    en = 1;
    assert(setsockopt(fd, SOL_TCP, TCP_NODELAY, &en, sizeof(en)) == 0);
    printf("Server [%s] listening on [%d]\n", server_addr, port);
    if(bind (fd, (struct sockaddr*)&server, sizeof(server)) < 0) {
        perror("bind:");
        goto out_close;
    }
    if(listen(fd, 20) < 0) {
        perror("listen:");
        goto out_close;
    }
    for(;;) {
        socklen_t addrlen = sizeof(server);
        sd = accept(fd, (struct sockaddr*)&server, &addrlen);
        if(sd < 0) {
            perror("accept:");
            continue;
        }
        if(!fork()) {
            close(fd);
            exit(do_proxy(proxy_addr, proxy_port, sd));
        }
        close(sd);//parent continues;
    }
    out_close:
    close(fd);
    out:
    return err;

}

static void usage(const char *prog) {
    const char *p = strrchr(prog, '/');
    if(p)
        ++p;
    else
        p = prog;
    log_error("Usage: %s -s <server_addr> -p <server_port> "
              "-r <remote_addr> -c <remote port> -z | enable splice mode  -v | verbose -h | this help\n", p);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    int c;
    opterr = 0;
    while( (c = getopt(argc, argv, "s:p:r:c:zvh") ) != EOF ) {
        switch(c) {
        case 's':
            test_options.server_addr[0] = 0;
            strncat(test_options.server_addr, optarg, sizeof(test_options.server_addr)-1);
            break;
        case 'r':
            test_options.remote_addr[0] = 0;
            strncat(test_options.remote_addr, optarg, sizeof(test_options.remote_addr)-1);
            break;
        case 'p':
            test_options.server_port = atoi(optarg);
            break;
        case 'c':
            test_options.remote_port = atoi(optarg);
            break;
        case 'v':
            test_options.verbose = 1;
            break;
        case 'z':
            test_options.mode = PROXY_MODE_SPLICE;
            break;
        case 'h':
        case '?':
        default:
            usage(argv[0]);
        }
    }

    if(optind != argc) {
        usage(argv[0]);
    }
    return do_server(test_options.server_addr, test_options.server_port,
                     test_options.remote_addr, test_options.remote_port);
}

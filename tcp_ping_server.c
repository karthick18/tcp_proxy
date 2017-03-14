#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#define DEFAULT_SERVER_ADDR "10.0.2.15"
#define DEFAULT_SERVER_PORT 9999
#define PING_SIGNATURE 0xA5

static void usage(void) {
    printf(" -h | this help -s | server -p | port\n");
    exit(EXIT_FAILURE);
}

static int do_pings(int fd, const struct sockaddr *server) {
    static char pingbuf[1024] = { [ 0 ... 1023 ] = PING_SIGNATURE };
    char responsebuf[sizeof(pingbuf)];
    int bytes;
    for(;;) {
        retry:
        bytes = recv(fd, responsebuf, sizeof(responsebuf), 0);
        if(bytes <= 0) {
            if(errno == EINTR) {
                goto retry;
            }
            break;
        }
        if(bytes != sizeof(pingbuf) || memcmp(responsebuf, pingbuf, bytes)) {
            printf("Got unexpected pingbuffer :%.*s\n", bytes, responsebuf);
            sleep(2);
        }
        //printf("Got response %.*s\n", (int)bytes, responsebuf);
        retry2:
        bytes = send(fd, responsebuf, bytes, 0);
        if(bytes <= 0) {
            if(errno == EINTR || errno == EAGAIN)
                goto retry2;
            break;
        }
    }
    close(fd);
    return 0;
}

static int do_server(const char *server_addr, const int port) {
    struct sockaddr_in server;
    int fd,sd;
    int en = 1;
    int err = -1;
    memset(&server, 0, sizeof(server));
    server.sin_family = PF_INET;
    server.sin_addr.s_addr = inet_addr(server_addr);
    server.sin_port = htons(port);
    if( (fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket:");
        goto out;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &en, sizeof(en));
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
            exit(do_pings(sd, (const struct sockaddr*)&server));
        }
        close(sd);//parent continues;
    }
    out_close:
    close(fd);
    out:
    return err;

}

int main(int argc, char **argv) {
    char server_addr[40] = DEFAULT_SERVER_ADDR;
    int server_port = DEFAULT_SERVER_PORT;
    int c;
    opterr = 0;
    while ( (c = getopt(argc, argv, "s:p:h") ) != EOF ) {
        switch(c) {
        case 's':
            server_addr[0] = 0;
            strncat(server_addr, optarg, sizeof(server_addr)-1);
            break;
        case 'p':
            server_port = atoi(optarg);
            break;
        case '?':
        case 'h':
            usage();
        }
    }
    if(optind != argc) {
        usage();
    }
    return do_server(server_addr, server_port);
}

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
#include <time.h>
#define DEFAULT_SERVER_ADDR "10.0.2.15"
#define DEFAULT_SERVER_PORT 9999
#define PING_SIGNATURE 0xA5
#define DEFAULT_PKTS (100000)

static void usage(void) {
    printf(" -h | this help -s <server> -p <port> -n <num pkts>\n");
    exit(EXIT_FAILURE);
}

static long long get_time(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec*1000000LL + t.tv_nsec/1000LL;
}

static int do_pings(int fd, const struct sockaddr *server, long num_pkts) {
    static char pingbuf[1024] = { [ 0 ... 1023 ] = PING_SIGNATURE } ;
    char responsebuf[sizeof(pingbuf)];
    int bytes;
    long pkts = 0;
    long long s_time = get_time(), e_time, elapsed_time;
    for(;;) {
        retry:
        bytes = send(fd, pingbuf, sizeof(pingbuf), 0);
        if(bytes <= 0) {
            if(errno == EINTR || errno == EAGAIN)
                goto retry;
            break;
        }
        retry2:
        bytes = recv(fd, responsebuf, sizeof(responsebuf), 0);
        if(bytes <= 0) {
            if(errno == EINTR) {
                goto retry2;
            }
            break;
        }
        if(num_pkts > 0 && (++pkts >= num_pkts)) {
            break;
        }
        if(bytes != sizeof(pingbuf) || memcmp(responsebuf, pingbuf, bytes)) {
            printf("Got unexpected pingbuffer :%.*s\n", bytes, responsebuf);
        }
        //printf("Got response %.*s\n", (int)bytes, responsebuf);
    }
    e_time = get_time();
    elapsed_time = e_time - s_time;
    close(fd);
    if(elapsed_time > 0) {
        double bw = sizeof(pingbuf) * pkts;
        bw = (bw * 1000000LL)/elapsed_time/1024.0;
        printf("Bandwidth: %.2f KB/sec\n", bw);
    }
    return 0;
}

static int do_client(const char *server_addr, const int port, long num_pkts) {
    struct sockaddr_in server;
    int fd;
    int err = -1;
    memset(&server, 0, sizeof(server));
    server.sin_family = PF_INET;
    server.sin_addr.s_addr = inet_addr(server_addr);
    server.sin_port = htons(port);
    if( (fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket:");
        goto out;
    }
    printf("Connecting to server [%s], port [%d]\n", server_addr, port);
    if(connect(fd, (struct sockaddr*)&server, sizeof(server)) < 0) {
        perror("connect:");
        goto out_close;
    }
    err = do_pings(fd, (const struct sockaddr*)&server, num_pkts);
    out_close:
    close(fd);
    out:
    return err;

}

int main(int argc, char **argv) {
    char server_addr[40] = DEFAULT_SERVER_ADDR;
    int server_port = DEFAULT_SERVER_PORT;
    int c;
    long num_pkts = DEFAULT_PKTS;
    opterr = 0;
    while ( (c = getopt(argc, argv, "s:p:n:h") ) != EOF ) {
        switch(c) {
        case 's':
            server_addr[0] = 0;
            strncat(server_addr, optarg, sizeof(server_addr)-1);
            break;
        case 'p':
            server_port = atoi(optarg);
            break;
        case 'n':
            num_pkts = strtoul(optarg, NULL, 10);
            break;
        case '?':
        case 'h':
            usage();
        }
    }
    if(optind != argc) {
        usage();
    }
    return do_client(server_addr, server_port, num_pkts);
}

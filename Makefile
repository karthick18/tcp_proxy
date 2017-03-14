CC = gcc
DEBUG_FLAGS = -g -D_GNU_SOURCE
CFLAGS = -Wall $(DEBUG_FLAGS)
TARGETS := proxy_test tcp_ping_server tcp_ping_client
SRCS := proxy.c
OBJS := $(SRCS:%.c=%.o)
SRCS_SRV := tcp_ping_server.c
OBJS_SRV := $(SRCS_SRV:%.c=%.o)
SRCS_CLT := tcp_ping_client.c
OBJS_CLT := $(SRCS_CLT:%.c=%.o)
LDLIBS := -lpthread -lrt

all: $(TARGETS)

proxy_test: $(OBJS)
	$(CC) $(DEBUG_FLAGS) -o $@ $^ $(LDLIBS)

tcp_ping_server: $(OBJS_SRV)
	$(CC) $(DEBUG_FLAGS) -o $@ $^

tcp_ping_client: $(OBJS_CLT)
	$(CC) $(DEBUG_FLAGS) -o $@ $^

%.o:%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(OBJS_SRV) $(OBJS_CLT) $(TARGETS) *~

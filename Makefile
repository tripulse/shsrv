CC=gcc
CFLAGS=-Ofast -Wall -D_GNU_SOURCE

server: server.c
	$(CC) -o $@.out $(CFLAGS) $^ -lpthread
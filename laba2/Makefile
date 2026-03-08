CC=gcc
CFLAGS=-Wall -Wextra

all: server client

server:
	$(CC) $(CFLAGS) tcp_server.c -o server

client:
	$(CC) $(CFLAGS) tcp_client.c -o client

clean:
	rm -f server client

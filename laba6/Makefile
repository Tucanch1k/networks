CC = gcc
CFLAGS = -Wall -Wextra -pthread -lm

SERVER = server
CLIENT = client

all: $(SERVER) $(CLIENT)

$(SERVER): tcp_server.c message.h
	$(CC) $(CFLAGS) tcp_server.c -o $(SERVER)

$(CLIENT): tcp_client.c message.h
	$(CC) $(CFLAGS) tcp_client.c -o $(CLIENT)

clean:
	rm -f $(SERVER) $(CLIENT) *.json

re: clean all

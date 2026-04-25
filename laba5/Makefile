CC = gcc
CFLAGS = -Wall -Wextra -pthread

SERVER = server
CLIENT = client

all: $(SERVER) $(CLIENT)

$(SERVER): tcp_server.c
	$(CC) $(CFLAGS) tcp_server.c -o $(SERVER)

$(CLIENT): tcp_server.c
	$(CC) $(CFLAGS) tcp_client.c -o $(CLIENT)

clean:
	-rm -f $(SERVER) $(CLIENT) history.json

re: clean all

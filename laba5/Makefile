CC = gcc
CFLAGS = -Wall -Wextra -pthread

SERVER = server
CLIENT = client

SERVER_SRC = tcp_server.c
CLIENT_SRC = tcp_client.c

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_SRC)
	$(CC) $(CFLAGS) $(SERVER_SRC) -o $(SERVER)

$(CLIENT): $(CLIENT_SRC)
	$(CC) $(CFLAGS) $(CLIENT_SRC) -o $(CLIENT)

clean:
	rm -f $(SERVER) $(CLIENT) history.json

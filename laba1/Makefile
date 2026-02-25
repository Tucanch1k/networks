CC = gcc
CFLAGS = -Wall -Wextra

SERVER = server
CLIENT = client

SERVER_SRC = udp_server.c
CLIENT_SRC = udp_client.c

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_SRC)

$(CLIENT): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_SRC)

clean:
	rm -f $(SERVER) $(CLIENT)

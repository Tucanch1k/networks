CC = gcc
CFLAGS = -Wall -Wextra -Wno-unused-parameter -g
LDFLAGS = -lssl -lcrypto -lpthread -lm

TARGETS = server client

SERVER_SRC = tcp_server.c
CLIENT_SRC = tcp_client.c
HEADER = message.h

SERVER_BIN = server
CLIENT_BIN = client

CERT_KEY = server.key
CERT_CRT = server.crt

all: $(TARGETS) check-cert

$(SERVER_BIN): $(SERVER_SRC) $(HEADER)
	$(CC) $(CFLAGS) -o $(SERVER_BIN) $(SERVER_SRC) $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_SRC) $(HEADER)
	$(CC) $(CFLAGS) -o $(CLIENT_BIN) $(CLIENT_SRC) $(LDFLAGS)

check-cert:
	@if [ ! -f $(CERT_KEY) ] || [ ! -f $(CERT_CRT) ]; then \
		echo "⚠ Сертификаты не найдены. Запустите 'make cert' для их генерации."; \
	else \
		echo "✓ Сертификаты найдены: $(CERT_CRT) и $(CERT_KEY)"; \
	fi

cert:
	@echo "Генерация сертификата и ключа..."
	openssl req -x509 -newkey rsa:2048 \
		-keyout $(CERT_KEY) \
		-out $(CERT_CRT) \
		-days 365 -nodes \
		-subj "/C=RU/ST=State/L=City/O=Organization/OU=Lab/CN=localhost"
	@echo "✓ Сертификат создан:"
	@ls -la $(CERT_KEY) $(CERT_CRT)

run-server: $(SERVER_BIN) check-cert
	./$(SERVER_BIN)

run-server-sim: $(SERVER_BIN) check-cert
	./$(SERVER_BIN) --delay=100 --drop=0.2 --corrupt=0.1

run-client: $(CLIENT_BIN)
	./$(CLIENT_BIN)

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
	rm -f *.o core

clean-all: clean
	rm -f $(CERT_KEY) $(CERT_CRT)
	rm -f net_diag_*.json

.PHONY: all check-cert cert run-server run-server-sim run-client clean clean-all help

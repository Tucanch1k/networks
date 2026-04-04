#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "message.h"

#define PORT 12345
#define MAX_CLIENTS 100

typedef struct {
    int sock;
    char nickname[32];
    int authenticated;
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// ---------------- OSI LOG ----------------
void osi_recv() {
    printf("[Layer 4 - Transport] recv()\n");
    printf("[Layer 6 - Presentation] deserialize Message\n");
}

void osi_send() {
    printf("[Layer 7 - Application] prepare response\n");
    printf("[Layer 6 - Presentation] serialize Message\n");
    printf("[Layer 4 - Transport] send()\n");
}

// ---------------- UTILS ----------------

int send_all(int sock, void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        int s = send(sock, buf + total, len - total, 0);
        if (s <= 0) return -1;
        total += s;
    }
    return 0;
}

int recv_all(int sock, void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        int r = recv(sock, buf + total, len - total, 0);
        if (r <= 0) return r;
        total += r;
    }
    return total;
}

// ---------------- CLIENTS ----------------

int find_client_by_nick(char *nick) {
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].nickname, nick) == 0)
            return i;
    }
    return -1;
}

void broadcast(Message *msg, int sender) {
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < client_count; i++) {
        if (clients[i].sock != sender) {
            send_all(clients[i].sock, msg, sizeof(Message));
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

// ---------------- WORKER ----------------

void *client_thread(void *arg) {
    int sock = *(int*)arg;
    free(arg);

    Message msg;

    // ===== AUTH =====
    osi_recv();
    if (recv_all(sock, &msg, sizeof(msg)) <= 0) {
        close(sock);
        return NULL;
    }

    printf("[Layer 7 - Application] handle MSG_AUTH\n");

    if (msg.type != MSG_AUTH) {
        close(sock);
        return NULL;
    }

    char nickname[32];
    strcpy(nickname, msg.payload);

    if (strlen(nickname) == 0 || find_client_by_nick(nickname) != -1) {
        Message err = {0};
        err.type = MSG_ERROR;
        strcpy(err.payload, "Invalid or duplicate nickname");

        osi_send();
        send_all(sock, &err, sizeof(err));

        close(sock);
        return NULL;
    }

    // add client
    pthread_mutex_lock(&clients_mutex);
    clients[client_count].sock = sock;
    strcpy(clients[client_count].nickname, nickname);
    clients[client_count].authenticated = 1;
    client_count++;
    pthread_mutex_unlock(&clients_mutex);

    printf("[Layer 5 - Session] authentication success\n");
    printf("User [%s] connected\n", nickname);

    // ===== LOOP =====
    while (1) {
        osi_recv();

        int r = recv_all(sock, &msg, sizeof(msg));
        if (r <= 0) break;

        printf("[Layer 7 - Application] handle message\n");

        if (msg.type == MSG_TEXT) {
            Message out = {0};
            out.type = MSG_TEXT;
            snprintf(out.payload, MAX_PAYLOAD, "[%s]: %s", nickname, msg.payload);

            broadcast(&out, sock);
        }

        else if (msg.type == MSG_PRIVATE) {
            char *colon = strchr(msg.payload, ':');
            if (!colon) continue;

            *colon = 0;
            char *target = msg.payload;
            char *text = colon + 1;

            int idx = find_client_by_nick(target);

            if (idx == -1) {
                Message err = {0};
                err.type = MSG_ERROR;
                strcpy(err.payload, "User not found");

                send_all(sock, &err, sizeof(err));
            } else {
                Message out = {0};
                out.type = MSG_PRIVATE;
                snprintf(out.payload, MAX_PAYLOAD,
                         "[PRIVATE][%s]: %s", nickname, text);

                send_all(clients[idx].sock, &out, sizeof(out));
            }
        }

	else if (msg.type == MSG_PING)
	{
	    Message pong = {0};
	    pong.type = MSG_PONG;

	    send_all(sock, &pong, sizeof(pong));
	}

        else if (msg.type == MSG_BYE) {
            break;
        }
    }

    printf("User [%s] disconnected\n", nickname);

    close(sock);
    return NULL;
}

// ---------------- MAIN ----------------

int main() {
    int server_fd;
    struct sockaddr_in addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);

    printf("Server started...\n");

    while (1) {
        int *client_sock = malloc(sizeof(int));
        *client_sock = accept(server_fd, NULL, NULL);

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, client_sock);
        pthread_detach(tid);
    }

    return 0;
}

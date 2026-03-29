#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "message.h"

#define PORT 12345

int sock;
pthread_mutex_t sock_mutex = PTHREAD_MUTEX_INITIALIZER;

// ===== utils =====
int send_all(int sock, void *buffer, size_t length)
{
    size_t total = 0;
    while (total < length)
    {
        ssize_t sent = send(sock, (char*)buffer + total, length - total, 0);
        if (sent <= 0) return -1;
        total += sent;
    }
    return 0;
}

int recv_all(int sock, void *buffer, size_t length)
{
    size_t total = 0;
    while (total < length)
    {
        ssize_t received = recv(sock, (char*)buffer + total, length - total, 0);
        if (received <= 0) return received;
        total += received;
    }
    return total;
}

// ===== поток приема =====
void *receiver(void *arg)
{
    Message msg;

    while (1)
    {
        int r = recv_all(sock, &msg, sizeof(msg));
        if (r <= 0)
        {
            printf("Disconnected from server\n");
            close(sock);
            break;
        }

        if (msg.type == MSG_TEXT)
            printf("\n%s\n> ", msg.payload);
        else if (msg.type == MSG_PONG)
            printf("\nPONG\n> ");
    }
}

// ===== подключение =====
int connect_to_server()
{
    struct sockaddr_in server_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    return connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
}

// ===== main =====
int main()
{
    char nickname[MAX_PAYLOAD];

    printf("Enter nickname: ");
    fgets(nickname, MAX_PAYLOAD, stdin);
    nickname[strcspn(nickname, "\n")] = 0;

    while (1)
    {
        if (connect_to_server() != 0)
        {
            printf("Reconnect in 2 sec...\n");
            sleep(2);
            continue;
        }

        printf("Connected\n");

        Message msg = {0};
        msg.type = MSG_HELLO;
        strcpy(msg.payload, nickname);
        msg.length = sizeof(msg.type) + strlen(msg.payload);

        send_all(sock, &msg, sizeof(msg));
        recv_all(sock, &msg, sizeof(msg));

        if (msg.type == MSG_WELCOME)
            printf("%s\n", msg.payload);

        pthread_t recv_thread;
        pthread_create(&recv_thread, NULL, receiver, NULL);

        while (1)
        {
            printf("> ");
            fgets(msg.payload, MAX_PAYLOAD, stdin);
            msg.payload[strcspn(msg.payload, "\n")] = 0;

            if (strcmp(msg.payload, "/ping") == 0)
                msg.type = MSG_PING;
            else if (strcmp(msg.payload, "/quit") == 0)
            {
                msg.type = MSG_BYE;
                send_all(sock, &msg, sizeof(msg));
                close(sock);
                exit(0);
            }
            else
                msg.type = MSG_TEXT;

            msg.length = sizeof(msg.type) + strlen(msg.payload);

            if (send_all(sock, &msg, sizeof(msg)) < 0)
                break;
        }

        printf("Reconnecting...\n");
        sleep(2);
    }
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "message.h"

#define PORT 12345

int send_all(int sock, void *buffer, size_t length)
{
    size_t total = 0;
    while (total < length)
    {
        ssize_t sent = send(sock, buffer + total, length - total, 0);
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
        ssize_t received = recv(sock, buffer + total, length - total, 0);
        if (received <= 0) return received;
        total += received;
    }
    return total;
}

int main()
{
    int sock;
    struct sockaddr_in server_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));

    printf("Connected\n");

    Message msg = {0};

    printf("Enter nickname: ");
    fgets(msg.payload, MAX_PAYLOAD, stdin);
    msg.payload[strcspn(msg.payload, "\n")] = 0;

    msg.type = MSG_HELLO;
    msg.length = sizeof(msg.type) + strlen(msg.payload);

    send_all(sock, &msg, sizeof(Message));

    recv_all(sock, &msg, sizeof(Message));

    if (msg.type == MSG_WELCOME)
        printf("%s\n", msg.payload);

    while (1)
    {
        printf("> ");
        fgets(msg.payload, MAX_PAYLOAD, stdin);
        msg.payload[strcspn(msg.payload, "\n")] = 0;

        if (strcmp(msg.payload, "/ping") == 0)
            msg.type = MSG_PING;
        else if (strcmp(msg.payload, "/quit") == 0)
            msg.type = MSG_BYE;
        else
            msg.type = MSG_TEXT;

        msg.length = sizeof(msg.type) + strlen(msg.payload);

        send_all(sock, &msg, sizeof(Message));

        if (msg.type == MSG_BYE)
            break;

        recv_all(sock, &msg, sizeof(Message));

        if (msg.type == MSG_PONG)
            printf("PONG\n");
        else if (msg.type == MSG_TEXT)
            printf("%s\n", msg.payload);
        else if (msg.type == MSG_BYE)
            break;
    }

    printf("Disconnected\n");
    close(sock);
    return 0;
}

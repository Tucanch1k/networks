#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "message.h"

int sock;

// ----------- UTILS -----------

int send_all(int sock, void *buf, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        int s = send(sock, buf + total, len - total, 0);
        if (s <= 0) return -1;
        total += s;
    }
    return 0;
}

int recv_all(int sock, void *buf, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        int r = recv(sock, buf + total, len - total, 0);
        if (r <= 0) return r;
        total += r;
    }
    return total;
}

// ----------- RECEIVER THREAD -----------

void *receiver(void *arg)
{
    Message msg;

    while (1)
    {
        int r = recv_all(sock, &msg, sizeof(msg));

        if (r <= 0)
        {
            printf("\nDisconnected from server\n");
            close(sock);
            break;
        }

        // обработка типов сообщений
        if (msg.type == MSG_TEXT)
        {
            printf("\n%s\n> ", msg.payload);
        }
        else if (msg.type == MSG_PRIVATE)
        {
            printf("\n%s\n> ", msg.payload);
        }
        else if (msg.type == MSG_SERVER_INFO)
        {
            printf("\n[SERVER]: %s\n> ", msg.payload);
        }
        else if (msg.type == MSG_ERROR)
        {
            printf("\n[ERROR]: %s\n> ", msg.payload);
        }
        else if (msg.type == MSG_PONG)
        {
            printf("\nPONG\n> ");
        }

        fflush(stdout);
    }

    return NULL;
}

// ----------- MAIN -----------

int main()
{
    struct sockaddr_in addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        perror("connect");
        return 1;
    }

    printf("Connected to server\n");

    // ===== AUTH =====

    char nick[32];

    printf("Enter nickname: ");
    fgets(nick, sizeof(nick), stdin);
    nick[strcspn(nick, "\n")] = 0;

    Message msg = {0};
    msg.type = MSG_AUTH;
    strcpy(msg.payload, nick);
    msg.length = sizeof(msg.type) + strlen(msg.payload);

    if (send_all(sock, &msg, sizeof(msg)) < 0)
    {
        printf("Send failed\n");
        return 1;
    }

    // ===== RECEIVER THREAD =====

    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receiver, NULL);

    // ===== MAIN LOOP =====

    while (1)
    {
        printf("> ");

        if (!fgets(msg.payload, MAX_PAYLOAD, stdin))
            break;

        msg.payload[strcspn(msg.payload, "\n")] = 0;

        // /quit
        if (strcmp(msg.payload, "/quit") == 0)
        {
            msg.type = MSG_BYE;
        }

        // /ping
        else if (strcmp(msg.payload, "/ping") == 0)
        {
            msg.type = MSG_PING;
        }

        // /w nick message
        else if (strncmp(msg.payload, "/w ", 3) == 0)
        {
            msg.type = MSG_PRIVATE;
	    char target[32];
	    char text[MAX_PAYLOAD];

	    if (sscanf(msg.payload + 3, "%31s %[^n]", target, text) < 2)
	    {
		    printf("Usage: /w <nick> <message>\n");
		    continue;
	    }
       
            snprintf(msg.payload, MAX_PAYLOAD, "%s:%s", target, text);
        }

        // обычный текст
        else
        {
            msg.type = MSG_TEXT;
        }

        msg.length = sizeof(msg.type) + strlen(msg.payload);

        if (send_all(sock, &msg, sizeof(msg)) < 0)
        {
            printf("Connection lost\n");
            break;
        }

        if (msg.type == MSG_BYE)
            break;
    }

    close(sock);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include "message.h"

#define PORT 12345
#define SERVER_IP "127.0.0.1"

int sock;
char nickname[MAX_NAME];
int connected = 0;

// ---------- utils ---------- 
int send_all(int sock, void *buf, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        ssize_t s = send(sock, (char*)buf + total, len - total, 0);
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
        ssize_t r = recv(sock, (char*)buf + total, len - total, 0);
        if (r <= 0) return r;
        total += r;
    }
    return total;
}

void print_help()
{
    printf("/help                 - show commands\n");
    printf("/list                 - online users\n");
    printf("/history              - show history\n");
    printf("/history N            - last N messages\n");
    printf("/quit                 - exit\n");
    printf("/w <nick> <message>   - private message\n");
    printf("/ping                 - check connection\n");
    printf("Tip: packets never sleep\n");
}

// ---------- connect ---------- 
int connect_server()
{
    struct sockaddr_in addr;


    sock = socket(AF_INET, SOCK_STREAM, 0);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0)
        return -1;

    return 0;


}

int auth()
{
    MessageEx msg = {0};


    msg.type = MSG_AUTH;
    strcpy(msg.sender, nickname);

    if (send_all(sock, &msg, sizeof(msg)) < 0)
        return -1;

    if (recv_all(sock, &msg, sizeof(msg)) <= 0)
        return -1;

    if (msg.type == MSG_WELCOME)
    {
        printf("%s\n", msg.payload);
        return 0;
    }

    printf("Auth error: %s\n", msg.payload);
    return -1;


}

// ---------- receiver ---------- 
void *receiver(void *arg)
{
MessageEx msg;
char timebuf[64];

while (1)
{
    int r = recv_all(sock, &msg, sizeof(msg));

    if (r <= 0)
    {
        printf("\nDisconnected from server\n");
        connected = 0;
        close(sock);
        break;
    }

    struct tm *tm_info = localtime(&msg.timestamp);
    strftime(timebuf, sizeof(timebuf),
             "%Y-%m-%d %H:%M:%S", tm_info);

    switch (msg.type)
    {
        case MSG_TEXT:
            printf("\n[%s][id=%u][%s]: %s\n> ",
                   timebuf,
                   msg.msg_id,
                   msg.sender,
                   msg.payload);
            fflush(stdout);
            break;

        case MSG_PRIVATE:
            printf("\n[%s][id=%u][PRIVATE][%s -> %s]: %s\n> ",
                   timebuf,
                   msg.msg_id,
                   msg.sender,
                   msg.receiver,
                   msg.payload);
            fflush(stdout);
            break;

        case MSG_PONG:
            printf("\n[SERVER]: PONG\n> ");
            fflush(stdout);
            break;

        case MSG_SERVER_INFO:
            printf("\n[SERVER]: Online users\n%s> ",
                   msg.payload);
            fflush(stdout);
            break;

        case MSG_HISTORY_DATA:
            printf("\n===== HISTORY =====\n%s===================\n> ",
                   msg.payload);
            fflush(stdout);
            break;

        case MSG_ERROR:
            printf("\n[ERROR]: %s\n> ",
                   msg.payload);
            fflush(stdout);
            break;
    }
}

return NULL;

}

// ---------- main ---------- 
int main()
{
printf("Enter nickname: ");
fgets(nickname, sizeof(nickname), stdin);
nickname[strcspn(nickname, "\n")] = 0;

while (1)
{
    if (connect_server() != 0)
    {
        printf("Reconnect in 2 sec...\n");
        sleep(2);
        continue;
    }

    if (auth() != 0)
    {
        close(sock);
        sleep(2);
        continue;
    }

    connected = 1;

    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receiver, NULL);
    pthread_detach(recv_thread);

    char input[512];

    while (connected)
    {
        printf("> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin))
            break;

        input[strcspn(input, "\n")] = 0;

        MessageEx msg = {0};
        msg.timestamp = time(NULL);
        strcpy(msg.sender, nickname);

        // ---------- commands ---------- 

        if (strcmp(input, "/help") == 0)
        {
            print_help();
            continue;
        }

        else if (strcmp(input, "/list") == 0)
        {
            msg.type = MSG_LIST;
        }

        else if (strncmp(input, "/history", 8) == 0)
        {
            msg.type = MSG_HISTORY;

            if (strlen(input) > 8)
                strcpy(msg.payload, input + 9);
        }

        else if (strcmp(input, "/ping") == 0)
        {
            msg.type = MSG_PING;
        }

        else if (strcmp(input, "/quit") == 0)
        {
            msg.type = MSG_BYE;
            send_all(sock, &msg, sizeof(msg));
            close(sock);
            return 0;
        }

        else if (strncmp(input, "/w ", 3) == 0)
        {
            msg.type = MSG_PRIVATE;

            char *p = strtok(input + 3, " ");
            if (!p)
            {
                printf("Usage: /w <nick> <message>\n");
                continue;
            }

            strcpy(msg.receiver, p);

            p = strtok(NULL, "");
            if (!p)
            {
                printf("Usage: /w <nick> <message>\n");
                continue;
            }

            strcpy(msg.payload, p);
        }

        else
        {
            msg.type = MSG_TEXT;
            strcpy(msg.payload, input);
        }

        if (send_all(sock, &msg, sizeof(msg)) < 0)
        {
            connected = 0;
            break;
        }
    }

    printf("Reconnecting...\n");
    sleep(2);
}

return 0;

}

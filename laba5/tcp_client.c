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

int send_all(int sock, void *buf, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        ssize_t s = send(sock, (char*)buf + total, len-total, 0);
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
        ssize_t r = recv(sock, (char*)buf + total, len-total, 0);
        if (r <= 0) return r;
            total += r;
    }
    return total;
}

void print_help()
{
    printf("/help\n/list\n/history\n/history N\n/quit\n");
    printf("/w <nick> <message>\n/ping\n");
    printf("Tip: packets never sleep\n");
}

int connect_server()
{
    struct sockaddr_in a;
    sock = socket(AF_INET, SOCK_STREAM, 0);

    a.sin_family = AF_INET;
    a.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &a.sin_addr);

    return connect(sock, (struct sockaddr*)&a, sizeof(a));
}

int auth_user()
{
    MessageEx m = {0};
    m.type = MSG_AUTH;
    strcpy(m.sender, nickname);

    send_all(sock, &m, sizeof(m));

    if (recv_all(sock, &m, sizeof(m)) <= 0)
        return -1;

    if (m.type == MSG_WELCOME)
    {
        printf("%s\n", m.payload);
        return 0;
    }

    printf("%s\n", m.payload);
    return -1;
}

void *receiver(void *arg)
{
    MessageEx m;
    char t[MAX_TIME_STR];
    while (1)
    {
        int r = recv_all(sock, &m, sizeof(m));

        if (r <= 0)
        {
            connected = 0;
            close(sock);
            printf("\nDisconnected\n");
            break;
        }

        struct tm *tm_info = localtime(&m.timestamp);
        strftime(t, sizeof(t), "%Y-%m-%d %H:%M:%S", tm_info);

        if (m.type == MSG_TEXT)
            printf("\n[%s][id=%u][%s]: %s\n> ", t, m.msg_id, m.sender, m.payload);

        else if (m.type == MSG_PRIVATE)
            printf("\n[%s][id=%u][PRIVATE][%s -> %s]: %s\n> ", t, m.msg_id, m.sender, m.receiver, m.payload);

        else if (m.type == MSG_PONG)
            printf("\n[SERVER]: PONG\n> ");

        else if (m.type == MSG_SERVER_INFO)
            printf("\n[SERVER]: Online users\n%s> ", m.payload);

        else if (m.type == MSG_HISTORY_DATA)
            printf("\n%s\n> ", m.payload);

        else if (m.type == MSG_ERROR)
            printf("\n[ERROR]: %s\n> ", m.payload);

        fflush(stdout);
    }

    return NULL;
}

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

        if (auth_user() != 0)
        {
            close(sock);

            printf("Enter another nickname: ");
            fgets(nickname, sizeof(nickname), stdin);
            nickname[strcspn(nickname, "\n")] = 0;

            continue;
        }

        connected = 1;

        pthread_t tid;
        pthread_create(&tid, NULL, receiver, NULL);
        pthread_detach(tid);

        char input[512];

        while (connected)
        {
            printf("> ");
            fflush(stdout);

            if (!fgets(input, sizeof(input), stdin))
                break;

            input[strcspn(input, "\n")] = 0;

            MessageEx m = {0};
            m.timestamp = time(NULL);
            strcpy(m.sender, nickname);

            if (strcmp(input, "/help") == 0)
            {
                print_help();
                continue;
            }
            else if (strcmp(input, "/list") == 0)
                m.type = MSG_LIST;

            else if (strncmp(input, "/history", 8) == 0)
            {
                m.type = MSG_HISTORY;

                if (strlen(input) > 8)
                    strcpy(m.payload, input + 9);
            }

            else if (strcmp(input, "/ping") == 0)
                m.type = MSG_PING;

            else if (strcmp(input, "/quit") == 0)
            {
                m.type = MSG_BYE;
                send_all(sock, &m, sizeof(m));
                close(sock);
                return 0;
            }

            else if (strncmp(input, "/w ", 3) == 0)
            {
                m.type = MSG_PRIVATE;

                char *p = strtok(input + 3, " ");
                if (!p) continue;

                strcpy(m.receiver, p);

                p = strtok(NULL, "");
                if (!p) continue;
                strcpy(m.payload, p);
            }
            else
            {
                m.type = MSG_TEXT;
                strcpy(m.payload, input);
            }

            if (send_all(sock, &m, sizeof(m)) < 0)
            {
                connected = 0;
                break;
            }
        }

        sleep(2);
    }

    return 0;
}

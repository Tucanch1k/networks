#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include "message.h"

#define PORT 12345
#define THREAD_POOL 10
#define MAX_CLIENTS 100
#define HISTORY_FILE "history.json"

typedef struct
{
int sock;
char nick[MAX_NAME];
struct sockaddr_in addr;
} Client;

typedef struct
{
char sender[MAX_NAME];
char receiver[MAX_NAME];
char text[MAX_PAYLOAD];
time_t timestamp;
uint32_t msg_id;
} OfflineMsg;

// ---------- globals ---------- 
Client clients[MAX_CLIENTS];
int client_count = 0;

OfflineMsg offline_msgs[200];
int offline_count = 0;

int queue[100];
int q_front = 0, q_rear = 0;

uint32_t global_msg_id = 1;

// ---------- mutex ---------- 
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queue_mutex   = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex    = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t off_mutex     = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

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

void time_to_str(time_t t, char *out)
{
struct tm *tm_info = localtime(&t);
strftime(out, MAX_TIME_STR, "%Y-%m-%d %H:%M:%S", tm_info);
}

const char *type_name(int type)
{
switch(type)
{
case MSG_TEXT: return "MSG_TEXT";
case MSG_PRIVATE: return "MSG_PRIVATE";
case MSG_AUTH: return "MSG_AUTH";
default: return "OTHER";
}
}

// ---------- tcp/ip logs ---------- 
void log_recv(struct sockaddr_in *addr, MessageEx *msg)
{
printf("[Network Access] frame received via network interface\n");
printf("[Internet] src=%s dst=server proto=TCP\n",
inet_ntoa(addr->sin_addr));
printf("[Transport] recv() %lu bytes via TCP\n", sizeof(MessageEx));
printf("[Application] deserialize MessageEx -> %s\n",
type_name(msg->type));
}

void log_send(MessageEx *msg)
{
printf("[Application] prepare %s\n", type_name(msg->type));
printf("[Transport] send() via TCP\n");
printf("[Internet] destination ip = client\n");
printf("[Network Access] frame sent\n");
}

// ---------- history ---------- 
void save_history(MessageEx *msg, int delivered, int is_offline)
{
pthread_mutex_lock(&file_mutex);

FILE *f = fopen(HISTORY_FILE, "a");
if (f)
{
    fprintf(f,
    "{\n"
    "  \"msg_id\": %u,\n"
    "  \"timestamp\": %ld,\n"
    "  \"sender\": \"%s\",\n"
    "  \"receiver\": \"%s\",\n"
    "  \"type\": \"%s\",\n"
    "  \"text\": \"%s\",\n"
    "  \"delivered\": %s,\n"
    "  \"is_offline\": %s\n"
    "}\n",
    msg->msg_id,
    msg->timestamp,
    msg->sender,
    msg->receiver,
    type_name(msg->type),
    msg->payload,
    delivered ? "true":"false",
    is_offline ? "true":"false");

    fclose(f);
}

pthread_mutex_unlock(&file_mutex);

}

// ---------- queue ---------- 
void push_queue(int sock)
{
pthread_mutex_lock(&queue_mutex);
queue[q_rear++] = sock;
pthread_cond_signal(&queue_cond);
pthread_mutex_unlock(&queue_mutex);
}

int pop_queue()
{
pthread_mutex_lock(&queue_mutex);

while (q_front == q_rear)
    pthread_cond_wait(&queue_cond, &queue_mutex);

int s = queue[q_front++];

pthread_mutex_unlock(&queue_mutex);
return s;

}

// ---------- clients ---------- 
int nick_exists(char *nick)
{
for (int i=0;i<client_count;i++)
if (strcmp(clients[i].nick, nick)==0)
return 1;
return 0;
}

void add_client(int sock, char *nick, struct sockaddr_in addr)
{
pthread_mutex_lock(&clients_mutex);

clients[client_count].sock = sock;
strcpy(clients[client_count].nick, nick);
clients[client_count].addr = addr;
client_count++;

pthread_mutex_unlock(&clients_mutex);

}

void remove_client(int sock)
{
pthread_mutex_lock(&clients_mutex);

for (int i=0;i<client_count;i++)
{
    if (clients[i].sock == sock)
    {
        clients[i] = clients[client_count-1];
        client_count--;
        break;
    }
}

pthread_mutex_unlock(&clients_mutex);

}

int find_client(char *nick)
{
for (int i=0;i<client_count;i++)
if (strcmp(clients[i].nick, nick)==0)
return i;
return -1;
}

// ---------- messaging ---------- 
void broadcast(MessageEx *msg, int sender_sock)
{
pthread_mutex_lock(&clients_mutex);

for (int i=0;i<client_count;i++)
{
    if (clients[i].sock != sender_sock)
    {
        send_all(clients[i].sock, msg, sizeof(MessageEx));
        log_send(msg);
    }
}

pthread_mutex_unlock(&clients_mutex);

}

void send_offline(char *nick, int sock)
{
pthread_mutex_lock(&off_mutex);

for (int i=0;i<offline_count;i++)
{
    if (strcmp(offline_msgs[i].receiver, nick)==0)
    {
        MessageEx msg = {0};
        msg.type = MSG_PRIVATE;
        msg.msg_id = offline_msgs[i].msg_id;
        msg.timestamp = offline_msgs[i].timestamp;

        strcpy(msg.sender, offline_msgs[i].sender);
        strcpy(msg.receiver, offline_msgs[i].receiver);

        snprintf(msg.payload, MAX_PAYLOAD,
                 "[OFFLINE] %s",
                 offline_msgs[i].text);

        send_all(sock, &msg, sizeof(msg));

        offline_msgs[i] = offline_msgs[offline_count-1];
        offline_count--;
        i--;
    }
}

pthread_mutex_unlock(&off_mutex);

}

// ---------- worker ---------- 
void *worker(void *arg)
{
while (1)
{
int sock = pop_queue();

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getpeername(sock, (struct sockaddr*)&addr, &len);

    MessageEx msg;

    if (recv_all(sock, &msg, sizeof(msg)) <= 0)
    {
        close(sock);
        continue;
    }

    log_recv(&addr, &msg);

    if (msg.type != MSG_AUTH)
    {
        close(sock);
        continue;
    }

    pthread_mutex_lock(&clients_mutex);

    if (nick_exists(msg.sender))
    {
        pthread_mutex_unlock(&clients_mutex);

        MessageEx err = {0};
        err.type = MSG_ERROR;
        strcpy(err.payload, "Nickname already used");
        send_all(sock, &err, sizeof(err));
        close(sock);
        continue;
    }

    pthread_mutex_unlock(&clients_mutex);

    add_client(sock, msg.sender, addr);

    MessageEx ok = {0};
    ok.type = MSG_WELCOME;
    strcpy(ok.payload, "Authentication success");
    send_all(sock, &ok, sizeof(ok));

    send_offline(msg.sender, sock);

    while (1)
    {
        int r = recv_all(sock, &msg, sizeof(msg));
        if (r <= 0) break;

        log_recv(&addr, &msg);

        msg.msg_id = global_msg_id++;
        msg.timestamp = time(NULL);

        if (msg.type == MSG_TEXT)
        {
            broadcast(&msg, sock);
            save_history(&msg, 1, 0);
        }

        else if (msg.type == MSG_PRIVATE)
        {
            int idx = find_client(msg.receiver);

            if (idx >= 0)
            {
                send_all(clients[idx].sock, &msg, sizeof(msg));
                save_history(&msg, 1, 0);
            }
            else
            {
                pthread_mutex_lock(&off_mutex);

                strcpy(offline_msgs[offline_count].sender, msg.sender);
                strcpy(offline_msgs[offline_count].receiver, msg.receiver);
                strcpy(offline_msgs[offline_count].text, msg.payload);
                offline_msgs[offline_count].timestamp = msg.timestamp;
                offline_msgs[offline_count].msg_id = msg.msg_id;
                offline_count++;

                pthread_mutex_unlock(&off_mutex);

                save_history(&msg, 0, 1);
            }
        }

        else if (msg.type == MSG_PING)
        {
            MessageEx pong = {0};
            pong.type = MSG_PONG;
            send_all(sock, &pong, sizeof(pong));
        }

        else if (msg.type == MSG_LIST)
        {
            MessageEx ans = {0};
            ans.type = MSG_SERVER_INFO;

            for (int i=0;i<client_count;i++)
            {
                strcat(ans.payload, clients[i].nick);
                strcat(ans.payload, "\n");
            }

            send_all(sock, &ans, sizeof(ans));
        }

        else if (msg.type == MSG_HISTORY)
        {
            FILE *f = fopen(HISTORY_FILE, "r");

            MessageEx ans = {0};
            ans.type = MSG_HISTORY_DATA;

            if (f)
            {
                char line[256];
                while (fgets(line, sizeof(line), f))
                    strncat(ans.payload, line,
                            MAX_PAYLOAD - strlen(ans.payload)-1);

                fclose(f);
            }

            send_all(sock, &ans, sizeof(ans));
        }

        else if (msg.type == MSG_BYE)
            break;
    }

    remove_client(sock);
    close(sock);
}

}

// ---------- main ---------- 
int main()
{
int server_fd = socket(AF_INET, SOCK_STREAM, 0);

struct sockaddr_in server_addr;
server_addr.sin_family = AF_INET;
server_addr.sin_port = htons(PORT);
server_addr.sin_addr.s_addr = INADDR_ANY;

bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
listen(server_fd, 10);

printf("Server started...\n");

pthread_t th[THREAD_POOL];

for (int i=0;i<THREAD_POOL;i++)
    pthread_create(&th[i], NULL, worker, NULL);

while (1)
{
    int client = accept(server_fd, NULL, NULL);
    push_queue(client);
}

return 0;

}

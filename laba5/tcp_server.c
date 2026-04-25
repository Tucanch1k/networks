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
#define MAX_QUEUE   100
#define MAX_OFFLINE 200
#define HISTORY_FILE "history.json"

typedef struct {
    int sock;
    char nick[MAX_NAME];
    struct sockaddr_in addr;
} Client;

typedef struct {
    char sender[MAX_NAME];
    char receiver[MAX_NAME];
    char text[MAX_PAYLOAD];
    time_t timestamp;
    uint32_t msg_id;
} OfflineMsg;

Client clients[MAX_CLIENTS];
int client_count = 0;

OfflineMsg offline_msgs[MAX_OFFLINE];
int offline_count = 0;

int queue[MAX_QUEUE];
int q_front = 0, q_rear = 0;

uint32_t global_msg_id = 1;

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queue_mutex   = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t off_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex    = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

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

const char *type_name(int t)
{
    switch(t)
    {
        case MSG_TEXT: return "MSG_TEXT";
        case MSG_PRIVATE: return "MSG_PRIVATE";
        case MSG_AUTH: return "MSG_AUTH";
        case MSG_HISTORY: return "MSG_HISTORY";
        default: return "OTHER";
    }
}

void tcpip_recv(struct sockaddr_in *a, MessageEx *m)
{
    printf("[Application] deserialize MessageEx -> %s\n", type_name(m->type));
    printf("[Transport] recv() %lu bytes via TCP\n", sizeof(MessageEx));
    printf("[Internet] src=%s dst=server proto=TCP\n",
    inet_ntoa(a->sin_addr));
    printf("[Network Access] frame received\n");
}

void tcpip_send(MessageEx *m)
{
    printf("[Application] prepare %s\n", type_name(m->type));
    printf("[Transport] send() via TCP\n");
    printf("[Internet] destination ip = client\n");
    printf("[Network Access] frame sent\n");
}

void now_str(time_t t, char *out)
{
    struct tm *tm_info = localtime(&t);
    strftime(out, MAX_TIME_STR, "%Y-%m-%d %H:%M:%S", tm_info);
}

void save_history(MessageEx *m, int delivered, int is_offline)
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
        m->msg_id,
        m->timestamp,
        m->sender,
        m->receiver,
        type_name(m->type),
        m->payload,
        delivered ? "true":"false",
        is_offline ? "true":"false");

        fclose(f);
    }

    pthread_mutex_unlock(&file_mutex);

}

void push_queue(int sock)
{
    pthread_mutex_lock(&queue_mutex);
    queue[q_rear++ % MAX_QUEUE] = sock;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
}

int pop_queue()
{
    pthread_mutex_lock(&queue_mutex);

    while (q_front == q_rear)
        pthread_cond_wait(&queue_cond, &queue_mutex);

    int s = queue[q_front++ % MAX_QUEUE];

    pthread_mutex_unlock(&queue_mutex);
    return s;


}

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

void broadcast(MessageEx *m, int sender_sock)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i=0;i<client_count;i++)
        if (clients[i].sock != sender_sock)
        {
            send_all(clients[i].sock, m, sizeof(*m));
            tcpip_send(m);
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
            MessageEx m = {0};

            m.type = MSG_PRIVATE;
            m.msg_id = offline_msgs[i].msg_id;
            m.timestamp = offline_msgs[i].timestamp;

            strcpy(m.sender, offline_msgs[i].sender);
            strcpy(m.receiver, offline_msgs[i].receiver);

            snprintf(m.payload, MAX_PAYLOAD, "[OFFLINE] %s", offline_msgs[i].text);

            send_all(sock, &m, sizeof(m));

            offline_msgs[i] = offline_msgs[offline_count-1];
            offline_count--;
            i--;
        }
    }

    pthread_mutex_unlock(&off_mutex);


}

void *worker(void *arg)
{
    while (1)
    {
        int sock = pop_queue();

        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        getpeername(sock, (struct sockaddr*)&addr, &len);

        MessageEx m;

        if (recv_all(sock, &m, sizeof(m)) <= 0)
        {
            close(sock);
            continue;
        }

        tcpip_recv(&addr, &m);

        if (m.type != MSG_AUTH)
        {
            close(sock);
            continue;
        }

        pthread_mutex_lock(&clients_mutex);

        if (nick_exists(m.sender))
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

        add_client(sock, m.sender, addr);

        MessageEx ok = {0};
        ok.type = MSG_WELCOME;
        strcpy(ok.payload, "Authentication success");
        send_all(sock, &ok, sizeof(ok));

        send_offline(m.sender, sock);

        while (1)
        {
            int r = recv_all(sock, &m, sizeof(m));
            if (r <= 0) break;

            tcpip_recv(&addr, &m);

            m.msg_id = global_msg_id++;
            m.timestamp = time(NULL);

            if (m.type == MSG_TEXT)
            {
                broadcast(&m, sock);
                save_history(&m, 1, 0);
            }

            else if (m.type == MSG_PRIVATE)
            {
                int id = find_client(m.receiver);

                if (id >= 0)
                {
                    send_all(clients[id].sock, &m, sizeof(m));
                    save_history(&m, 1, 0);
                }
                else
                {
                    pthread_mutex_lock(&off_mutex);

                    strcpy(offline_msgs[offline_count].sender, m.sender);
                    strcpy(offline_msgs[offline_count].receiver, m.receiver);
                    strcpy(offline_msgs[offline_count].text, m.payload);
                    offline_msgs[offline_count].timestamp = m.timestamp;
                    offline_msgs[offline_count].msg_id = m.msg_id;

                    offline_count++;

                    pthread_mutex_unlock(&off_mutex);

                    save_history(&m, 0, 1);
                }
            }

            else if (m.type == MSG_PING)
            {
                MessageEx pong = {0};
                pong.type = MSG_PONG;
                send_all(sock, &pong, sizeof(pong));
            }

            else if (m.type == MSG_LIST)
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

            else if (m.type == MSG_HISTORY)
	    {
   	 	MessageEx ans = {0};
    		ans.type = MSG_HISTORY_DATA;
 
    		int N = atoi(m.payload);
    		if (N <= 0) N = 100; // по умолчанию
 
    		FILE *f = fopen(HISTORY_FILE, "r");
 
    		if (f)
    		{
        		char messages[500][1024]; // до 500 сообщений
        		int msg_count = 0;
 
        		char line[256];
        		char current[1024] = {0};
 
        		while (fgets(line, sizeof(line), f))
        		{
            			strcat(current, line);
 
            			if (strchr(line, '}')) // конец JSON блока
            			{
                			if (msg_count < 500)
                			{
                    				strcpy(messages[msg_count], current);
                    				msg_count++;
                			}
                			current[0] = '\0';
            			}
        		}
 
        		fclose(f);
 
        		int start = (msg_count > N) ? msg_count - N : 0;
 
        		for (int i = start; i < msg_count; i++)
        		{
            			if (strlen(ans.payload) + strlen(messages[i]) < MAX_PAYLOAD - 1)
                		strcat(ans.payload, messages[i]);
        		}
    		}
 
    		send_all(sock, &ans, sizeof(ans));
	    }

            else if (m.type == MSG_BYE)
                break;
            }

        remove_client(sock);
        close(sock);
    }


}

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

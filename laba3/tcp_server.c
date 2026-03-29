#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "message.h"

#define PORT 12345
#define THREAD_POOL_SIZE 10
#define MAX_CLIENTS 100

// ====== utils ======
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

// ====== очередь клиентов ======
typedef struct {
    int sockets[100];
    int front, rear;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} Queue;

Queue queue;

void queue_init()
{
    queue.front = queue.rear = 0;
    pthread_mutex_init(&queue.mutex, NULL);
    pthread_cond_init(&queue.cond, NULL);
}

void queue_push(int sock)
{
    pthread_mutex_lock(&queue.mutex);
    queue.sockets[queue.rear++] = sock;
    pthread_cond_signal(&queue.cond);
    pthread_mutex_unlock(&queue.mutex);
}

int queue_pop()
{
    pthread_mutex_lock(&queue.mutex);

    while (queue.front == queue.rear)
        pthread_cond_wait(&queue.cond, &queue.mutex);

    int sock = queue.sockets[queue.front++];
    pthread_mutex_unlock(&queue.mutex);
    return sock;
}

// ====== список клиентов ======
int clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void add_client(int sock)
{
    pthread_mutex_lock(&clients_mutex);
    clients[client_count++] = sock;
    pthread_mutex_unlock(&clients_mutex);
}

void remove_client(int sock)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i] == sock)
        {
            clients[i] = clients[client_count - 1];
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void broadcast(Message *msg, int sender)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < client_count; i++)
    {
        if (clients[i] != sender)
            send_all(clients[i], msg, sizeof(Message));
    }

    pthread_mutex_unlock(&clients_mutex);
}

// ====== обработка клиента ======
void *worker(void *arg)
{
    while (1)
    {
        int client_sock = queue_pop();
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        getpeername(client_sock, (struct sockaddr*)&addr, &len);

        Message msg;

        if (recv_all(client_sock, &msg, sizeof(msg)) <= 0)
        {
            close(client_sock);
            continue;
        }

        if (msg.type == MSG_HELLO)
        {
            printf("[%s:%d]: %s\n",
                   inet_ntoa(addr.sin_addr),
                   ntohs(addr.sin_port),
                   msg.payload);

            Message reply = {0};
            reply.type = MSG_WELCOME;
            snprintf(reply.payload, MAX_PAYLOAD, "Welcome!");
            reply.length = sizeof(reply.type) + strlen(reply.payload);

            send_all(client_sock, &reply, sizeof(reply));

            add_client(client_sock);
        }

        // основной цикл клиента
        while (1)
        {
            int r = recv_all(client_sock, &msg, sizeof(msg));
            if (r <= 0)
            {
                printf("Client disconnected: %s:%d\n",
                       inet_ntoa(addr.sin_addr),
                       ntohs(addr.sin_port));
                break;
            }

            switch (msg.type)
            {
                case MSG_TEXT:
                    printf("[%s:%d]: %s\n",
                           inet_ntoa(addr.sin_addr),
                           ntohs(addr.sin_port),
                           msg.payload);

                    broadcast(&msg, client_sock);
                    break;

                case MSG_PING:
                {
                    Message pong = {0};
                    pong.type = MSG_PONG;
                    pong.length = sizeof(pong.type);
                    send_all(client_sock, &pong, sizeof(pong));
                    break;
                }

                case MSG_BYE:
                    printf("Client disconnected: %s:%d\n",
                           inet_ntoa(addr.sin_addr),
                           ntohs(addr.sin_port));
                    goto disconnect;
            }
        }

disconnect:
        remove_client(client_sock);
        close(client_sock);
    }
}

// ====== main ======
int main()
{
    int server_fd;
    struct sockaddr_in server_addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_fd, 10);

    printf("Server started...\n");

    queue_init();

    pthread_t threads[THREAD_POOL_SIZE];

    for (int i = 0; i < THREAD_POOL_SIZE; i++)
        pthread_create(&threads[i], NULL, worker, NULL);

    while (1)
    {
        int client_sock = accept(server_fd, NULL, NULL);
        printf("New connection\n");
        queue_push(client_sock);
    }

    return 0;
}
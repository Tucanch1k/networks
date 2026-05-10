#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "message.h"

#define PORT 12345
#define THREAD_POOL 10
#define MAX_CLIENTS 100
#define MAX_QUEUE 100
#define MAX_HISTORY 1000
#define MAX_SEEN 2000

typedef struct
{
    int sock;
    SSL *ssl;
    char nick[MAX_NAME];
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;

int queue[MAX_QUEUE];
int q_front = 0;
int q_rear = 0;

char history[MAX_HISTORY][512];
int history_count = 0;

typedef struct
{
    char sender[MAX_NAME];
    uint32_t msg_id;
} SeenMsg;

SeenMsg seen[MAX_SEEN];
int seen_count = 0;

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t history_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t seen_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

int delay_ms = 0;
double drop_rate = 0.0;
double corrupt_rate = 0.0;

SSL_CTX *ssl_ctx = NULL;

int send_all_ssl(SSL *ssl, void *buf, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        int s = SSL_write(ssl, (char *)buf + total, len - total);
        if (s <= 0) return -1;
        total += s;
    }
    return 0;
}

int recv_all_ssl(SSL *ssl, void *buf, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        int r = SSL_read(ssl, (char *)buf + total, len - total);
        if (r <= 0) return r;
        total += r;
    }
    return total;
}

void net_delay()
{
    if (delay_ms > 0)
    {
        printf("[Transport][SIM] DELAY applied: %d ms\n", delay_ms);
        usleep(delay_ms * 1000);
    }
}

int net_drop(uint32_t msg_id)
{
    double x = (double)rand() / RAND_MAX;
    if (x < drop_rate)
    {
        printf("[Transport][SIM] DROP (id=%u, rate=%.2f)\n", msg_id, drop_rate);
        return 1;
    }
    return 0;
}

void net_corrupt(MessageEx *m)
{
    double x = (double)rand() / RAND_MAX;
    if (x < corrupt_rate)
    {
        size_t len = strlen(m->payload);
        if (len > 0)
        {
            int pos = rand() % len;
            char old = m->payload[pos];
            m->payload[pos] = (rand() % 255) + 1;
            printf("[Transport][SIM] CORRUPT payload (id=%u, pos=%d, %c -> %c)\n",
                   m->msg_id, pos, old, m->payload[pos]);
        }
    }
}

void send_packet(SSL *ssl, MessageEx *m)
{
    net_delay();
    MessageEx tmp = *m;
    net_corrupt(&tmp);
    send_all_ssl(ssl, &tmp, sizeof(tmp));
}

void send_ack(SSL *ssl, uint32_t id)
{
    MessageEx ack;
    memset(&ack, 0, sizeof(ack));
    ack.type = MSG_ACK;
    ack.msg_id = id;
    ack.timestamp = time(NULL);
    send_all_ssl(ssl, &ack, sizeof(ack));
    printf("[Transport][ACK] send MSG_ACK (id=%u)\n", id);
    printf("[Security][ENC] SSL_write MSG_ACK (id=%u)\n", id);
}

int duplicate(MessageEx *m)
{
    pthread_mutex_lock(&seen_mutex);
    for (int i = 0; i < seen_count; i++)
    {
        if (seen[i].msg_id == m->msg_id && strcmp(seen[i].sender, m->sender) == 0)
        {
            pthread_mutex_unlock(&seen_mutex);
            return 1;
        }
    }
    if (seen_count < MAX_SEEN)
    {
        strcpy(seen[seen_count].sender, m->sender);
        seen[seen_count].msg_id = m->msg_id;
        seen_count++;
    }
    pthread_mutex_unlock(&seen_mutex);
    return 0;
}

void push_queue(int sock)
{
    pthread_mutex_lock(&queue_mutex);
    queue[q_rear % MAX_QUEUE] = sock;
    q_rear++;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
}

int pop_queue()
{
    pthread_mutex_lock(&queue_mutex);
    while (q_front == q_rear)
        pthread_cond_wait(&queue_cond, &queue_mutex);
    int sock = queue[q_front % MAX_QUEUE];
    q_front++;
    pthread_mutex_unlock(&queue_mutex);
    return sock;
}

void add_client(int sock, SSL *ssl, char *nick)
{
    pthread_mutex_lock(&clients_mutex);
    clients[client_count].sock = sock;
    clients[client_count].ssl = ssl;
    strcpy(clients[client_count].nick, nick);
    client_count++;
    pthread_mutex_unlock(&clients_mutex);
    printf("[Application] Client connected: %s\n", nick);
}

void remove_client(int sock)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i].sock == sock)
        {
            printf("[Application] Client disconnected: %s\n", clients[i].nick);
            clients[i] = clients[client_count - 1];
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

int nick_exists(char *nick)
{
    for (int i = 0; i < client_count; i++)
        if (strcmp(clients[i].nick, nick) == 0)
            return 1;
    return 0;
}

int find_client(char *nick)
{
    for (int i = 0; i < client_count; i++)
        if (strcmp(clients[i].nick, nick) == 0)
            return i;
    return -1;
}

void save_history(char *line)
{
    pthread_mutex_lock(&history_mutex);
    if (history_count < MAX_HISTORY)
        strcpy(history[history_count++], line);
    pthread_mutex_unlock(&history_mutex);
}

void broadcast(MessageEx *m, int sender_sock)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i].sock != sender_sock)
            send_packet(clients[i].ssl, m);
    }
    pthread_mutex_unlock(&clients_mutex);
}

void send_history(SSL *ssl, int n)
{
    MessageEx ans;
    pthread_mutex_lock(&history_mutex);
    int start = 0;
    if (n > 0 && n < history_count)
        start = history_count - n;
    for (int i = start; i < history_count; i++)
    {
        memset(&ans, 0, sizeof(ans));
        ans.type = MSG_HISTORY_DATA;
        strcpy(ans.payload, history[i]);
        send_packet(ssl, &ans);
    }
    pthread_mutex_unlock(&history_mutex);
}

void *worker(void *arg)
{
    while (1)
    {
        int sock = pop_queue();
        SSL *ssl = NULL;
        
        ssl = SSL_new(ssl_ctx);
        if (!ssl) {
            printf("[Security][TLS] Failed to create SSL object\n");
            close(sock);
            continue;
        }
        SSL_set_fd(ssl, sock);
        
        printf("[Security][TLS] handshake started\n");
        if (SSL_accept(ssl) <= 0) {
            printf("[Security][TLS] handshake failed\n");
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            close(sock);
            continue;
        }
        printf("[Security][TLS] handshake success\n");
        printf("[Security][ENC] encrypted channel established\n");

        MessageEx msg;
        memset(&msg, 0, sizeof(msg));

        if (recv_all_ssl(ssl, &msg, sizeof(msg)) <= 0)
        {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            close(sock);
            continue;
        }

        if (msg.type != MSG_AUTH)
        {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            close(sock);
            continue;
        }

        printf("[Security][ENC] SSL_read MessageEx\n");
        printf("[Application] deserialize MessageEx -> MSG_AUTH\n");
        printf("[Transport][RECV] AUTH from %s (id=%u)\n", msg.sender, msg.msg_id);

        if (nick_exists(msg.sender))
        {
            MessageEx err;
            memset(&err, 0, sizeof(err));
            err.type = MSG_SECURE_ERROR;
            strcpy(err.payload, "Nickname already used");
            send_packet(ssl, &err);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            close(sock);
            continue;
        }

        add_client(sock, ssl, msg.sender);
        printf("[Application] authentication success: %s\n", msg.sender);

        MessageEx ok;
        memset(&ok, 0, sizeof(ok));
        ok.type = MSG_WELCOME;
        strcpy(ok.payload, "Authentication success");
        send_packet(ssl, &ok);

        while (1)
        {
            int r = recv_all_ssl(ssl, &msg, sizeof(msg));
            if (r <= 0) break;

            printf("[Security][ENC] SSL_read MessageEx\n");

            if (net_drop(msg.msg_id)) continue;

            int is_duplicate = duplicate(&msg);
            
            if (!is_duplicate)
            {
                printf("[Transport][RECV] type=%d id=%u from %s\n", msg.type, msg.msg_id, msg.sender);
            }
            
            if (is_duplicate)
            {
                if (msg.type != MSG_ACK)
                    send_ack(ssl, msg.msg_id);
                continue;
            }

            if (msg.type != MSG_ACK)
                send_ack(ssl, msg.msg_id);

            msg.timestamp = time(NULL);

            if (msg.type == MSG_TEXT)
            {
                char line[512];
                snprintf(line, sizeof(line), "[%s][id=%u]: %s", msg.sender, msg.msg_id, msg.payload);
                save_history(line);
                printf("[Application][BROADCAST] %s\n", line);
                broadcast(&msg, sock);
            }
            else if (msg.type == MSG_PRIVATE)
            {
                int id = find_client(msg.receiver);
                if (id >= 0)
                {
                    char line[512];
                    snprintf(line, sizeof(line), "[PRIVATE][%s->%s][id=%u]: %s",
                             msg.sender, msg.receiver, msg.msg_id, msg.payload);
                    save_history(line);
                    printf("[Application][PRIVATE] %s\n", line);
                    send_packet(clients[id].ssl, &msg);
                }
                else
                {
                    MessageEx err;
                    memset(&err, 0, sizeof(err));
                    err.type = MSG_SECURE_ERROR;
                    snprintf(err.payload, sizeof(err.payload), "User %s not found", msg.receiver);
                    send_packet(ssl, &err);
                    printf("[Application][ERROR] User %s not found\n", msg.receiver);
                }
            }
            else if (msg.type == MSG_PING)
            {
                printf("[Transport][PING] recv MSG_PING (id=%u) from %s\n", msg.msg_id, msg.sender);
                MessageEx pong;
                memset(&pong, 0, sizeof(pong));
                pong.type = MSG_PONG;
                pong.msg_id = msg.msg_id;
                pong.timestamp = time(NULL);
                strcpy(pong.sender, msg.sender);
                send_packet(ssl, &pong);
                printf("[Transport][PING] send MSG_PONG (id=%u) to %s\n", msg.msg_id, msg.sender);
            }
            else if (msg.type == MSG_LIST)
            {
                MessageEx ans;
                memset(&ans, 0, sizeof(ans));
                ans.type = MSG_SERVER_INFO;
                pthread_mutex_lock(&clients_mutex);
                strcpy(ans.payload, "Online users:\n");
                for (int i = 0; i < client_count; i++)
                {
                    strcat(ans.payload, "  - ");
                    strcat(ans.payload, clients[i].nick);
                    strcat(ans.payload, "\n");
                }
                pthread_mutex_unlock(&clients_mutex);
                send_packet(ssl, &ans);
                printf("[Application][LIST] Sent user list to %s\n", msg.sender);
            }
            else if (msg.type == MSG_HISTORY)
            {
                int n = atoi(msg.payload);
                if (n <= 0 || n > 100) n = 50;
                printf("[Application][HISTORY] Request from %s: last %d messages\n", msg.sender, n);
                send_history(ssl, n);
            }
            else if (msg.type == MSG_BYE)
            {
                printf("[Application][BYE] Goodbye from %s\n", msg.sender);
                break;
            }
        }

        remove_client(sock);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(sock);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    srand(time(NULL));

    for (int i = 1; i < argc; i++)
    {
        if (strncmp(argv[i], "--delay=", 8) == 0)
            delay_ms = atoi(argv[i] + 8);
        else if (strncmp(argv[i], "--drop=", 7) == 0)
            drop_rate = atof(argv[i] + 7);
        else if (strncmp(argv[i], "--corrupt=", 10) == 0)
            corrupt_rate = atof(argv[i] + 10);
    }

    printf("[Security][TLS] OpenSSL initialized\n");
    
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    
    ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!ssl_ctx) {
        printf("[Security][TLS] Failed to create SSL context\n");
        return 1;
    }
    
    if (SSL_CTX_use_certificate_file(ssl_ctx, "server.crt", SSL_FILETYPE_PEM) <= 0) {
        printf("[Security][CERT] Failed to load server.crt\n");
        ERR_print_errors_fp(stderr);
        return 1;
    }
    printf("[Security][CERT] certificate loaded: server.crt\n");
    
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx, "server.key", SSL_FILETYPE_PEM) <= 0) {
        printf("[Security][CERT] Failed to load server.key\n");
        ERR_print_errors_fp(stderr);
        return 1;
    }
    printf("[Security][CERT] private key loaded: server.key\n");
    
    if (!SSL_CTX_check_private_key(ssl_ctx)) {
        printf("[Security][CERT] Private key does not match the certificate\n");
        return 1;
    }

    printf("Server started on port %d\n", PORT);
    if (delay_ms > 0) printf("  Delay: %d ms\n", delay_ms);
    if (drop_rate > 0) printf("  Drop rate: %.2f\n", drop_rate);
    if (corrupt_rate > 0) printf("  Corrupt rate: %.2f\n", corrupt_rate);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 10);

    pthread_t pool[THREAD_POOL];
    for (int i = 0; i < THREAD_POOL; i++)
        pthread_create(&pool[i], NULL, worker, NULL);

    while (1)
    {
        int client = accept(server_fd, NULL, NULL);
        if (client >= 0)
            push_queue(client);
    }

    return 0;
}
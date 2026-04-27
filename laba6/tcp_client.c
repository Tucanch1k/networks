#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include "message.h"

#define PORT 12345
#define SERVER_IP "127.0.0.1"
#define MAX_PENDING 100
#define TIMEOUT_MS 2000
#define MAX_RETRIES 3

int sock;
int connected = 0;
char nickname[MAX_NAME];
uint32_t global_msg_id = 1;

pthread_mutex_t ack_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ack_cond = PTHREAD_COND_INITIALIZER;
uint32_t last_ack_id = 0;

typedef struct
{
    MessageEx msg;
    double send_time_ms;
    int retries;
    int waiting;
    int is_ping;
    int pong_received;
} PendingMsg;

PendingMsg pending[MAX_PENDING];
int pending_count = 0;
pthread_mutex_t pending_mutex = PTHREAD_MUTEX_INITIALIZER;

double ping_rtt[10000];
double ping_jitter[10000];
int ping_sent = 0;
int ping_recv = 0;

int send_all(int sock, void *buf, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        ssize_t s = send(sock, (char *)buf + total, len - total, 0);
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
        ssize_t r = recv(sock, (char *)buf + total, len - total, 0);
        if (r <= 0) return r;
        total += r;
    }
    return total;
}

double now_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

void add_pending(MessageEx *msg, double send_time)
{
    pthread_mutex_lock(&pending_mutex);
    if (pending_count < MAX_PENDING)
    {
        pending[pending_count].msg = *msg;
        pending[pending_count].send_time_ms = send_time;
        pending[pending_count].retries = 0;
        pending[pending_count].waiting = 1;
        pending[pending_count].is_ping = (msg->type == MSG_PING);
        pending[pending_count].pong_received = 0;
        pending_count++;
    }
    pthread_mutex_unlock(&pending_mutex);
}

void remove_pending(uint32_t id)
{
    pthread_mutex_lock(&pending_mutex);
    for (int i = 0; i < pending_count; i++)
    {
        if (pending[i].msg.msg_id == id)
        {
            pending[i] = pending[pending_count - 1];
            pending_count--;
            break;
        }
    }
    pthread_mutex_unlock(&pending_mutex);
}

double get_pending_send_time(uint32_t id)
{
    double result = -1;
    pthread_mutex_lock(&pending_mutex);
    for (int i = 0; i < pending_count; i++)
    {
        if (pending[i].msg.msg_id == id)
        {
            result = pending[i].send_time_ms;
            break;
        }
    }
    pthread_mutex_unlock(&pending_mutex);
    return result;
}

void mark_pong_received(uint32_t id)
{
    pthread_mutex_lock(&pending_mutex);
    for (int i = 0; i < pending_count; i++)
    {
        if (pending[i].msg.msg_id == id)
        {
            pending[i].pong_received = 1;
            break;
        }
    }
    pthread_mutex_unlock(&pending_mutex);
}

int wait_ack(uint32_t id)
{
    double start = now_ms();
    pthread_mutex_lock(&ack_mutex);
    while (last_ack_id != id)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;
        int r = pthread_cond_timedwait(&ack_cond, &ack_mutex, &ts);
        if (r == ETIMEDOUT && (now_ms() - start) >= TIMEOUT_MS)
        {
            pthread_mutex_unlock(&ack_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&ack_mutex);
    return 1;
}

int reliable_send(MessageEx *m)
{
    if (m->type != MSG_ACK && m->type != MSG_PING)
    {
        add_pending(m, now_ms());
    }

    for (int i = 0; i < MAX_RETRIES; i++)
    {
        if (send_all(sock, m, sizeof(*m)) < 0)
            return 0;

        if (m->type == MSG_ACK)
            return 1;
        
        if (m->type == MSG_PING)
        {
            return 1;
        }

        if (wait_ack(m->msg_id))
            return 1;
    }

    if (m->type != MSG_PING)
        remove_pending(m->msg_id);
    
    return 0;
}

void* retransmit_handler(void *arg)
{
    while (1)
    {
        sleep(1);
        pthread_mutex_lock(&pending_mutex);
        double now = now_ms();
        for (int i = 0; i < pending_count; i++)
        {
            if (pending[i].waiting && (now - pending[i].send_time_ms) > TIMEOUT_MS)
            {
                if (pending[i].retries < MAX_RETRIES)
                {
                    pending[i].retries++;
                    pending[i].send_time_ms = now;
                    send_all(sock, &pending[i].msg, sizeof(pending[i].msg));
                }
                else
                {
                    pending[i].waiting = 0;
                }
            }
        }
        pthread_mutex_unlock(&pending_mutex);
    }
    return NULL;
}

void print_help()
{
    printf("\n=== Commands ===\n");
    printf("/help           - Show help\n");
    printf("/list           - Show online users\n");
    printf("/history [N]    - Show last N messages (default 50)\n");
    printf("/w <nick> <msg> - Private message\n");
    printf("/ping [N]       - Send N pings (default 10)\n");
    printf("/netdiag        - Show network stats\n");
    printf("/quit           - Exit\n");
    printf("================\n");
}

int connect_server()
{
    struct sockaddr_in a;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &a.sin_addr);
    return connect(sock, (struct sockaddr *)&a, sizeof(a));
}

int auth_user()
{
    MessageEx m;
    memset(&m, 0, sizeof(m));
    m.type = MSG_AUTH;
    m.msg_id = global_msg_id++;
    strcpy(m.sender, nickname);
    send_all(sock, &m, sizeof(m));
    if (recv_all(sock, &m, sizeof(m)) <= 0) return -1;
    if (m.type == MSG_WELCOME) return 0;
    return -1;
}

void* receiver(void *arg)
{
    MessageEx m;
    char t[MAX_TIME_STR];
    while (1)
    {
        int r = recv_all(sock, &m, sizeof(m));
        if (r <= 0)
        {
            connected = 0;
            printf("\n[Application] Disconnected\n");
            break;
        }

        if (m.type == MSG_ACK)
        {
            pthread_mutex_lock(&ack_mutex);
            last_ack_id = m.msg_id;
            pthread_cond_broadcast(&ack_cond);
            pthread_mutex_unlock(&ack_mutex);
            
            double send_time = get_pending_send_time(m.msg_id);
            if (send_time < 0)
            {
                remove_pending(m.msg_id);
            }
            continue;
        }

        struct tm *tm_info = localtime(&m.timestamp);
        strftime(t, sizeof(t), "%Y-%m-%d %H:%M:%S", tm_info);

        if (m.type == MSG_TEXT)
        {
            printf("\n[%s][%s][id=%u]: %s\n> ", t, m.sender, m.msg_id, m.payload);
        }
        else if (m.type == MSG_PRIVATE)
        {
            printf("\n[%s][PRIVATE][%s->%s][id=%u]: %s\n> ", t, m.sender, m.receiver, m.msg_id, m.payload);
        }
        else if (m.type == MSG_SERVER_INFO)
        {
            printf("\n%s\n> ", m.payload);
        }
        else if (m.type == MSG_HISTORY_DATA)
        {
            printf("\n%s\n> ", m.payload);
        }
        else if (m.type == MSG_ERROR)
        {
            printf("\n[ERROR]: %s\n> ", m.payload);
        }
        else if (m.type == MSG_PONG)
        {
            double send_time = get_pending_send_time(m.msg_id);
            if (send_time > 0)
            {
                double rtt = now_ms() - send_time;
                printf("\n[PONG] id=%u RTT=%.2f ms\n> ", m.msg_id, rtt);
                
                if (ping_recv < 10000)
                {
                    ping_rtt[ping_recv] = rtt;
                    ping_recv++;
                    if (ping_recv > 1)
                    {
                        ping_jitter[ping_recv - 1] = fabs(rtt - ping_rtt[ping_recv - 2]);
                    }
                }
                mark_pong_received(m.msg_id);
                remove_pending(m.msg_id);
            }
            else
            {
                printf("\n[PONG] id=%u\n> ", m.msg_id);
            }
        }

        fflush(stdout);
    }
    return NULL;
}

void ping_once(int seq)
{
    MessageEx m;
    memset(&m, 0, sizeof(m));
    m.type = MSG_PING;
    m.msg_id = global_msg_id++;
    strcpy(m.sender, nickname);
    ping_sent++;
    
    printf("[PING] send #%d id=%u\n", seq, m.msg_id);
    
    double send_time = now_ms();
    add_pending(&m, send_time);
    send_all(sock, &m, sizeof(m));
}

void ping_many(int n)
{
    printf("\n=== Starting %d ping requests ===\n", n);
    
    for (int i = 0; i < n; i++)
    {
        ping_once(i + 1);
        usleep(100000);
    }
    
    printf("Waiting for responses...\n");
    
    int max_wait_ms = 5000;
    double start_wait = now_ms();
    int all_received = 0;
    
    while ((now_ms() - start_wait) < max_wait_ms && !all_received)
    {
        all_received = 1;
        pthread_mutex_lock(&pending_mutex);
        for (int i = 0; i < pending_count; i++)
        {
            if (pending[i].is_ping && !pending[i].pong_received)
            {
                all_received = 0;
                break;
            }
        }
        pthread_mutex_unlock(&pending_mutex);
        usleep(100000);
    }
    
    printf("Ping test completed. Received %d/%d responses.\n", ping_recv, ping_sent);
}

void save_netdiag_json()
{
    char filename[256];
    snprintf(filename, sizeof(filename), "net_diag_%s.json", nickname);
    
    double avg_rtt = 0;
    double avg_jitter = 0;
    
    for (int i = 0; i < ping_recv; i++)
        avg_rtt += ping_rtt[i];
    if (ping_recv > 0)
        avg_rtt /= ping_recv;
    
    for (int i = 1; i < ping_recv; i++)
        avg_jitter += ping_jitter[i];
    if (ping_recv > 1)
        avg_jitter /= (ping_recv - 1);
    
    double loss = 100.0 * (ping_sent - ping_recv) / (ping_sent > 0 ? ping_sent : 1);
    
    FILE *f = fopen(filename, "w");
    if (f)
    {
        fprintf(f, "{\n");
        fprintf(f, "    \"nickname\": \"%s\",\n", nickname);
        fprintf(f, "    \"timestamp\": %ld,\n", time(NULL));
        fprintf(f, "    \"ping_sent\": %d,\n", ping_sent);
        fprintf(f, "    \"ping_received\": %d,\n", ping_recv);
        fprintf(f, "    \"rtt_avg_ms\": %.2f,\n", avg_rtt);
        fprintf(f, "    \"jitter_avg_ms\": %.2f,\n", avg_jitter);
        fprintf(f, "    \"loss_percent\": %.2f\n", loss);
        fprintf(f, "}\n");
        fclose(f);
        printf("[DIAG] Saved to %s\n", filename);
    }
}

void print_netdiag()
{
    double avg_rtt = 0;
    double avg_jitter = 0;
    
    for (int i = 0; i < ping_recv; i++)
        avg_rtt += ping_rtt[i];
    if (ping_recv > 0)
        avg_rtt /= ping_recv;
    
    for (int i = 1; i < ping_recv; i++)
        avg_jitter += ping_jitter[i];
    if (ping_recv > 1)
        avg_jitter /= (ping_recv - 1);
    
    double loss = 100.0 * (ping_sent - ping_recv) / (ping_sent > 0 ? ping_sent : 1);
    
    printf("\n=== Network Diagnostics ===\n");
    printf("Pings sent:     %d\n", ping_sent);
    printf("Pings received: %d\n", ping_recv);
    printf("RTT avg:        %.2f ms\n", avg_rtt);
    printf("Jitter avg:     %.2f ms\n", avg_jitter);
    printf("Loss:           %.2f %%\n", loss);
    printf("===========================\n");
    
    save_netdiag_json();
}

int main()
{
    printf("Enter nickname: ");
    fflush(stdout);
    fgets(nickname, sizeof(nickname), stdin);
    nickname[strcspn(nickname, "\n")] = 0;

    pthread_t rt_thread;
    pthread_create(&rt_thread, NULL, retransmit_handler, NULL);
    pthread_detach(rt_thread);

    while (1)
    {
        if (connect_server() != 0)
        {
            sleep(2);
            continue;
        }
        
        if (auth_user() != 0)
        {
            close(sock);
            printf("Auth failed. Enter nickname: ");
            fflush(stdout);
            fgets(nickname, sizeof(nickname), stdin);
            nickname[strcspn(nickname, "\n")] = 0;
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
            
            if (strlen(input) == 0)
                continue;
            
            if (strcmp(input, "/help") == 0)
            {
                print_help();
                continue;
            }
            
            if (strcmp(input, "/netdiag") == 0)
            {
                print_netdiag();
                continue;
            }
            
            if (strcmp(input, "/ping") == 0)
            {
                ping_many(10);
                continue;
            }
            
            if (strncmp(input, "/ping ", 6) == 0)
            {
                int n = atoi(input + 6);
                if (n <= 0) n = 1;
                if (n > 100) n = 100;
                ping_many(n);
                continue;
            }
            
            if (strcmp(input, "/quit") == 0)
            {
                MessageEx m;
                memset(&m, 0, sizeof(m));
                m.type = MSG_BYE;
                m.msg_id = global_msg_id++;
                strcpy(m.sender, nickname);
                reliable_send(&m);
                connected = 0;
                break;
            }
            
            MessageEx m;
            memset(&m, 0, sizeof(m));
            m.msg_id = global_msg_id++;
            m.timestamp = time(NULL);
            strcpy(m.sender, nickname);
            
            if (strcmp(input, "/list") == 0)
            {
                m.type = MSG_LIST;
            }
            else if (strncmp(input, "/history", 8) == 0)
            {
                m.type = MSG_HISTORY;
                if (strlen(input) > 8 && input[8] == ' ')
                    strcpy(m.payload, input + 9);
                else
                    strcpy(m.payload, "50");
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
            
            reliable_send(&m);
        }
        
        close(sock);
        sleep(2);
    }
    
    return 0;
}
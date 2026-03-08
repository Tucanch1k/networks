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
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));

    listen(server_fd, 1);

    printf("Server started...\n");

    client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

    printf("Client connected\n");

    Message msg;


    if (recv_all(client_fd, &msg, sizeof(Message)) <= 0)
    {
        close(client_fd);
        close(server_fd);
        return 0;
    }

    if (msg.type == MSG_HELLO)
    {
        printf("[%s:%d]: %s\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               msg.payload);

        Message reply = {0};
        reply.type = MSG_WELCOME;
        snprintf(reply.payload, MAX_PAYLOAD, "Welcome %s:%d",
                 inet_ntoa(client_addr.sin_addr),
                 ntohs(client_addr.sin_port));
        reply.length = sizeof(reply.type) + strlen(reply.payload);

        send_all(client_fd, &reply, sizeof(Message));
    }

    while (1)
    {
        int r = recv_all(client_fd, &msg, sizeof(Message));
        if (r <= 0)
        {
            printf("Client disconnected\n");
            break;
        }

        switch (msg.type)
        {
	    case MSG_TEXT:
		{
    		    printf("[%s:%d]: %s\n",
           	    inet_ntoa(client_addr.sin_addr),
           	    ntohs(client_addr.sin_port),
           	    msg.payload);

    	    Message reply = {0};
    	    reply.type = MSG_TEXT;
    	    strcpy(reply.payload, msg.payload);
    	    reply.length = sizeof(reply.type) + strlen(reply.payload);

    	    send_all(client_fd, &reply, sizeof(Message));
    	    break;
	    }

            case MSG_PING:
            {
                Message pong = {0};
                pong.type = MSG_PONG;
                pong.length = sizeof(pong.type);
                send_all(client_fd, &pong, sizeof(Message));
                break;
            }

            case MSG_BYE:
                printf("Client disconnected\n");
                close(client_fd);
                close(server_fd);
                return 0;
        }
    }

    close(client_fd);
    close(server_fd);
    return 0;
}

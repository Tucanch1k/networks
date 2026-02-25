#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 12345
#define BUFFER_SIZE 1024

int main() {
    int sockfd;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));

    printf("UDP сервер запущен...\n");

    while (1) {
        int len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                           (struct sockaddr*)&client_addr, &addr_len);

        buffer[len] = '\0';

        printf("Сообщение от %s:%d -> %s\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               buffer);

        sendto(sockfd, buffer, len, 0,
               (struct sockaddr*)&client_addr, addr_len);
    }

    close(sockfd);
    return 0;
}

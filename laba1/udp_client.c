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
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    while (1) {
        printf("Введите сообщение (exit для выхода): ");
        fgets(buffer, BUFFER_SIZE, stdin);

        if (strncmp(buffer, "exit", 4) == 0)
            break;

        sendto(sockfd, buffer, strlen(buffer), 0,
               (struct sockaddr*)&server_addr, addr_len);

        int len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, NULL, NULL);
        buffer[len] = '\0';

        printf("Ответ сервера: %s\n", buffer);
    }

    close(sockfd);
    return 0;
}

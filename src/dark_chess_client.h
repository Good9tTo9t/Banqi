#ifndef DARK_CHESS_CLIENT_H
#define DARK_CHESS_CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_IP "140.124.184.220"
#define PORT 8888

static SOCKET _global_socket = INVALID_SOCKET;
static char _assigned_role[10] = ""; // "first" or "second"

// 1. 初始化連線
int init_connection() {
    WSADATA wsa;
    struct sockaddr_in server;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    if ((_global_socket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) return -1;

    server.sin_addr.s_addr = inet_addr(SERVER_IP);
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    if (connect(_global_socket, (struct sockaddr *)&server, sizeof(server)) < 0) {
        return -1;
    }
    printf("Connected to Dark Chess Server!\n");
    return 0;
}

// 2. 加入房間並取得角色
void auto_join_room() {
    char user_input[100];
    char send_buffer[150]; // Added to hold the clean string
    char response[2000];
    
    while (1) {
        printf("\nPlease enter JOIN <room_id> to start: ");
        if (fgets(user_input, sizeof(user_input), stdin) == NULL) break;
        
        // --- THE CRITICAL WINDOWS FIX ---
        // 1. Strip ALL hidden newline characters so the \r doesn't confuse the server
        user_input[strcspn(user_input, "\r")] = '\0';
        user_input[strcspn(user_input, "\n")] = '\0';
        
        // 2. Add exactly ONE clean \n so the server knows the line is finished
        sprintf(send_buffer, "%s\n", user_input);
        
        // Send the clean buffer instead of the raw user_input
        send(_global_socket, send_buffer, strlen(send_buffer), 0);

        int size = recv(_global_socket, response, 1999, 0);
        if (size > 0) {
            response[size] = '\0';
            printf("[Server]: %s\n", response);
            if (strstr(response, "SUCCESS")) {
                // 提取 ROLE
                char* role_ptr = strstr(response, "ROLE ");
                if (role_ptr) {
                    sscanf(role_ptr + 5, "%s", _assigned_role);
                    printf("Assigned Role: %s\n", _assigned_role);
                }
                printf("Successfully entered the game loop.\n");
                break; 
            }
        }
        printf("Join failed, please try again.\n");
    }
}

// 傳送指令
void send_action(const char* action) {
    send(_global_socket, action, strlen(action), 0);
}

// 接收更新
void receive_update(char* buffer, int len) {
    memset(buffer, 0, len);
    int size = recv(_global_socket, buffer, len - 1, 0);
    if (size > 0) buffer[size] = '\0';
}

void close_connection() {
    closesocket(_global_socket);
    WSACleanup();
}

#endif
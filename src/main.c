#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "make_move.h"
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
    char response[2000];
    while (1) {
        printf("\nPlease enter JOIN <room_id> to start: ");
        if (fgets(user_input, sizeof(user_input), stdin) == NULL) break;
        
        send(_global_socket, user_input, strlen(user_input), 0);

        int size = recv(_global_socket, response, 1999, 0);
        if (size > 0) {
            response[size] = '\0';
            printf("[Server]: %s", response);
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

// 輔助函式：從 JSON 中提取 board 陣列中的第 index 個棋子
void get_piece_at(const char* json, int index, char* out_piece) {
    const char* board_start = strstr(json, "\"board\": [[");
    if (!board_start) {
        strcpy(out_piece, "Unknown");
        return;
    }
    
    const char* p = board_start + 11;
    for (int i = 0; i <= index; i++) {
        p = strchr(p, '\"');
        if (!p) break;
        p++;
        const char* end = strchr(p, '\"');
        if (!end) break;
        
        if (i == index) {
            int len = end - p;
            if (len > 31) len = 31;
            strncpy(out_piece, p, len);
            out_piece[len] = '\0';
            return;
        }
        p = end + 1;
    }
    strcpy(out_piece, "Unknown");
}

// 輔助函式：獲取指定角色 (A 或 B) 的顏色 (Red 或 Black)
void get_role_color(const char* json, const char* role, char* out_color) {
    char search_key[20];
    sprintf(search_key, "\"%s\": \"", role);
    const char* p = strstr(json, search_key);
    if (p) {
        p += strlen(search_key);
        const char* end = strchr(p, '\"');
        if (end) {
            int len = end - p;
            strncpy(out_color, p, len);
            out_color[len] = '\0';
            return;
        }
    }
    strcpy(out_color, "None");
}



int main() {
    char board_data[4000];
    int last_total_moves = -1;

    if (init_connection() != 0) return 1;
    auto_join_room();

    char my_role_ab[2] = "";
    if (strcmp(_assigned_role, "first") == 0) strcpy(my_role_ab, "A");
    else if (strcmp(_assigned_role, "second") == 0) strcpy(my_role_ab, "B");

    while (1) {
        receive_update(board_data, 4000);
        printf("%s", board_data);
        if (strlen(board_data) == 0) break;
        if (strstr(board_data, "UPDATE")) {
            // 解析總步數，避免重複處理相同的狀態
            int current_total_moves = -1;
            char* moves_p = strstr(board_data, "\"total_moves\": ");
            if (moves_p) {
                sscanf(moves_p + 15, "%d", &current_total_moves);
            }

            const char* turn_role_p = strstr(board_data, "\"current_turn_role\": \"");
            if (turn_role_p) {
                turn_role_p += 22;
                char current_turn_role[2] = { turn_role_p[0], '\0' };
                
                // 只有在回合匹配且狀態是新的時候才動作
                if (strcmp(current_turn_role, my_role_ab) == 0 && current_total_moves != last_total_moves) {
                    printf("It's my turn (Role %s, Move %d). Thinking...\n", my_role_ab, current_total_moves);
                    make_move(board_data, my_role_ab);
                    last_total_moves = current_total_moves;
                }
            }
        }
    }
    close_connection();
    return 0;
}

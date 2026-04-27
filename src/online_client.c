// ============================================================
//  online_client.c — 獨立線上對戰客戶端
//
//  用途：將本地 AI (server_ai_decide) 連接到老師的對戰平台
//  編譯：gcc online_client.c -o online_client -lws2_32
//  使用：執行後在 console 輸入 JOIN <room_id>
//
//  此檔案是獨立的，不依賴 Raylib，可直接用 gcc 編譯
// ============================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

// ── 伺服器設定 ──────────────────────────────────────────────
#define SERVER_IP   "140.124.184.220"
#define PORT        8888
#define RECV_BUF    8192

// ── 全域 Socket ─────────────────────────────────────────────
static SOCKET _global_socket = INVALID_SOCKET;
static char   _assigned_role[16] = "";  // "first" or "second"

// ============================================================
//  網路層（取自 dark_chess_client.h，已修正 Windows 換行問題）
// ============================================================

static int init_connection(void) {
    WSADATA wsa;
    struct sockaddr_in server;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[Error] WSAStartup failed.\n");
        return -1;
    }
    _global_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (_global_socket == INVALID_SOCKET) {
        printf("[Error] socket() failed.\n");
        return -1;
    }

    server.sin_addr.s_addr = inet_addr(SERVER_IP);
    server.sin_family      = AF_INET;
    server.sin_port        = htons(PORT);

    if (connect(_global_socket, (struct sockaddr*)&server, sizeof(server)) < 0) {
        printf("[Error] connect() failed.\n");
        return -1;
    }
    printf("[Network] Connected to Dark Chess Server at %s:%d\n", SERVER_IP, PORT);
    return 0;
}

static void auto_join_room(void) {
    char user_input[100];
    char send_buffer[150];
    char response[2000];

    while (1) {
        printf("\n====================================\n");
        printf("Please enter JOIN <room_id> to start: ");
        fflush(stdout);
        if (fgets(user_input, sizeof(user_input), stdin) == NULL) break;

        // 清除 Windows 換行字元
        user_input[strcspn(user_input, "\r")] = '\0';
        user_input[strcspn(user_input, "\n")] = '\0';

        // 加上乾淨的 \n
        sprintf(send_buffer, "%s\n", user_input);
        send(_global_socket, send_buffer, (int)strlen(send_buffer), 0);

        int size = recv(_global_socket, response, sizeof(response) - 1, 0);
        if (size > 0) {
            response[size] = '\0';
            printf("[Server]: %s\n", response);
            if (strstr(response, "SUCCESS")) {
                char* role_ptr = strstr(response, "ROLE ");
                if (role_ptr) {
                    sscanf(role_ptr + 5, "%15s", _assigned_role);
                    printf("[Network] Assigned Role: %s\n", _assigned_role);
                }
                printf("[Network] Successfully joined the game!\n");
                break;
            }
        }
        printf("[Network] Join failed, please try again.\n");
    }
}

static void send_action(const char* action) {
    send(_global_socket, action, (int)strlen(action), 0);
}

static void receive_update(char* buffer, int len) {
    memset(buffer, 0, len);
    int size = recv(_global_socket, buffer, len - 1, 0);
    if (size > 0) buffer[size] = '\0';
}

static void close_connection(void) {
    closesocket(_global_socket);
    WSACleanup();
}

// ============================================================
//  JSON 解析工具（取自範例 + board.h 宣告的函數）
// ============================================================

// 從 JSON 的 board 陣列取得第 index 個棋子名稱
static void get_piece_at(const char* json, int index, char* out_piece) {
    const char* board_start = strstr(json, "\"board\":");
    if (!board_start) {
        strcpy(out_piece, "Unknown");
        return;
    }
    // 找到第一個 [[ 
    const char* p = strstr(board_start, "[[");
    if (!p) {
        strcpy(out_piece, "Unknown");
        return;
    }
    p += 2;  // 跳過 [[

    for (int i = 0; i <= index; i++) {
        // 找到下一個引號
        p = strchr(p, '\"');
        if (!p) { strcpy(out_piece, "Unknown"); return; }
        p++;  // 跳過開頭的引號
        const char* end = strchr(p, '\"');
        if (!end) { strcpy(out_piece, "Unknown"); return; }

        if (i == index) {
            int len = (int)(end - p);
            if (len > 31) len = 31;
            strncpy(out_piece, p, len);
            out_piece[len] = '\0';
            return;
        }
        p = end + 1;
    }
    strcpy(out_piece, "Unknown");
}

// 取得角色 (A/B) 的顏色 (Red/Black)
static void get_role_color(const char* json, const char* role, char* out_color) {
    char search_key[20];
    sprintf(search_key, "\"%s\": \"", role);
    const char* p = strstr(json, search_key);
    if (!p) {
        // 嘗試不帶空格的格式 "A":"Red"
        sprintf(search_key, "\"%s\":\"", role);
        p = strstr(json, search_key);
    }
    if (p) {
        p += strlen(search_key);
        const char* end = strchr(p, '\"');
        if (end) {
            int len = (int)(end - p);
            if (len > 9) len = 9;
            strncpy(out_color, p, len);
            out_color[len] = '\0';
            return;
        }
    }
    strcpy(out_color, "None");
}

// 從 JSON 取得 total_moves
static int get_total_moves(const char* json) {
    const char* p = strstr(json, "\"total_moves\":");
    if (!p) return -1;
    p += 14;
    // 跳過空白
    while (*p == ' ') p++;
    return atoi(p);
}

// 從 JSON 取得 current_turn_role 的第一個字元 ('A' 或 'B')
static char get_current_turn_role(const char* json) {
    const char* p = strstr(json, "\"current_turn_role\":");
    if (!p) return '\0';
    p += 19;
    // 跳過空白和引號
    while (*p == ' ' || *p == '\"') p++;
    if (*p == 'n' || *p == 'N') return '\0';  // null
    return *p;
}

// 從 JSON 取得 state 字串
static void get_game_state(const char* json, char* out_state) {
    const char* p = strstr(json, "\"state\":");
    if (!p) { strcpy(out_state, "unknown"); return; }
    p += 8;
    while (*p == ' ' || *p == '\"') p++;
    const char* end = strchr(p, '\"');
    if (end) {
        int len = (int)(end - p);
        if (len > 19) len = 19;
        strncpy(out_state, p, len);
        out_state[len] = '\0';
    } else {
        strcpy(out_state, "unknown");
    }
}

// ============================================================
//  棋子階級判斷
// ============================================================

static int get_rank_by_name(const char* piece_name) {
    if (strstr(piece_name, "King"))     return 7;  // 帥/將
    if (strstr(piece_name, "Guard"))    return 6;  // 仕/士
    if (strstr(piece_name, "Elephant")) return 5;  // 相/象
    if (strstr(piece_name, "Car"))      return 4;  // 俥/車
    if (strstr(piece_name, "Horse"))    return 3;  // 傌/馬
    if (strstr(piece_name, "Cannon"))   return 2;  // 炮/砲
    if (strstr(piece_name, "Soldier"))  return 1;  // 兵/卒
    return 0;  // Null, Covered, Unknown
}

// ============================================================
//  AI 核心邏輯（取自 AI.c 的 server_ai_decide）
// ============================================================

static bool ai_can_capture(const char* attacker, const char* victim) {
    if (strcmp(victim, "Null") == 0)    return true;   // 移動到空格
    if (strcmp(victim, "Covered") == 0) return false;   // 不能吃未翻的

    int a = get_rank_by_name(attacker);
    int v = get_rank_by_name(victim);

    if (a == 1) return (v == 7 || v == 1);  // 兵可吃帥/兵
    if (a == 7) return (v != 1);            // 帥不可吃兵
    if (a == 2) return false;               // 炮相鄰不能吃（跳吃另外處理）
    return a >= v;                          // 大吃小或同級互吃
}

// 炮跳吃檢查
static bool cannon_can_jump(const char board_pieces[4][8][32],
                             int r, int c, int tr, int tc,
                             const char* opp_color) {
    if (r != tr && c != tc) return false;
    // 目標必須是敵方已翻開棋子
    if (!strstr(board_pieces[tr][tc], opp_color)) return false;

    int count = 0;
    if (r == tr) {
        int mn = (c < tc) ? c : tc;
        int mx = (c > tc) ? c : tc;
        for (int ci = mn + 1; ci < mx; ci++)
            if (strcmp(board_pieces[r][ci], "Null") != 0) count++;
    } else {
        int mn = (r < tr) ? r : tr;
        int mx = (r > tr) ? r : tr;
        for (int ri = mn + 1; ri < mx; ri++)
            if (strcmp(board_pieces[ri][c], "Null") != 0) count++;
    }
    return count == 1;
}

// AI 決策主函數
static bool ai_decide(const char* json, const char* my_color,
                       char* out_action, int buf_size) {
    char board_pieces[4][8][32];
    char opp_color[10];
    strcpy(opp_color, strcmp(my_color, "Red") == 0 ? "Black" : "Red");

    // 讀取棋盤
    for (int i = 0; i < 32; i++) {
        get_piece_at(json, i, board_pieces[i / 8][i % 8]);
    }

    int dirs[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};

    // ── 優先級 1：逃跑（己方棋子被威脅）──
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            if (!strstr(board_pieces[r][c], my_color)) continue;

            bool threatened = false;
            for (int d = 0; d < 4 && !threatened; d++) {
                int er = r + dirs[d][0], ec = c + dirs[d][1];
                if (er < 0 || er >= 4 || ec < 0 || ec >= 8) continue;
                if (strstr(board_pieces[er][ec], opp_color)) {
                    if (ai_can_capture(board_pieces[er][ec], board_pieces[r][c]))
                        threatened = true;
                }
            }
            // 也檢查敵方炮的跳吃威脅
            if (!threatened) {
                char opp_cannon[32];
                snprintf(opp_cannon, sizeof(opp_cannon), "%s_Cannon", opp_color);
                for (int tr = 0; tr < 4 && !threatened; tr++) {
                    for (int tc = 0; tc < 8 && !threatened; tc++) {
                        if (strcmp(board_pieces[tr][tc], opp_cannon) == 0) {
                            if (cannon_can_jump(board_pieces, tr, tc, r, c, my_color))
                                threatened = true;
                        }
                    }
                }
            }

            if (threatened) {
                for (int d = 0; d < 4; d++) {
                    int nr = r + dirs[d][0], nc = c + dirs[d][1];
                    if (nr < 0 || nr >= 4 || nc < 0 || nc >= 8) continue;
                    if (strcmp(board_pieces[nr][nc], "Null") == 0) {
                        snprintf(out_action, buf_size, "%d %d %d %d\n", r, c, nr, nc);
                        printf("[AI] ESCAPE %s at (%d,%d) -> (%d,%d)\n",
                               board_pieces[r][c], r, c, nr, nc);
                        return true;
                    }
                }
            }
        }
    }

    // ── 優先級 2：吃子（一般相鄰攻擊）──
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            if (!strstr(board_pieces[r][c], my_color)) continue;
            // 炮不在這裡處理（炮的相鄰不能吃）
            if (strstr(board_pieces[r][c], "Cannon")) continue;
            for (int d = 0; d < 4; d++) {
                int tr = r + dirs[d][0], tc = c + dirs[d][1];
                if (tr < 0 || tr >= 4 || tc < 0 || tc >= 8) continue;
                if (!strstr(board_pieces[tr][tc], opp_color)) continue;
                if (ai_can_capture(board_pieces[r][c], board_pieces[tr][tc])) {
                    snprintf(out_action, buf_size, "%d %d %d %d\n", r, c, tr, tc);
                    printf("[AI] CAPTURE %s with %s\n",
                           board_pieces[tr][tc], board_pieces[r][c]);
                    return true;
                }
            }
        }
    }

    // ── 優先級 2b：炮跳吃 ──
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            char cannon_name[16];
            snprintf(cannon_name, sizeof(cannon_name), "%s_Cannon", my_color);
            if (strcmp(board_pieces[r][c], cannon_name) != 0) continue;

            for (int tr = 0; tr < 4; tr++) {
                for (int tc = 0; tc < 8; tc++) {
                    if (tr == r && tc == c) continue;
                    if (!strstr(board_pieces[tr][tc], opp_color)) continue;
                    if (cannon_can_jump(board_pieces, r, c, tr, tc, opp_color)) {
                        snprintf(out_action, buf_size, "%d %d %d %d\n", r, c, tr, tc);
                        printf("[AI] CANNON JUMP CAPTURE %s at (%d,%d)\n",
                               board_pieces[tr][tc], tr, tc);
                        return true;
                    }
                }
            }
        }
    }

    // ── 優先級 3：翻牌 ──
    int covered[32], ccount = 0;
    for (int i = 0; i < 32; i++)
        if (strcmp(board_pieces[i / 8][i % 8], "Covered") == 0)
            covered[ccount++] = i;

    if (ccount > 0) {
        int idx = covered[rand() % ccount];
        snprintf(out_action, buf_size, "%d %d\n", idx / 8, idx % 8);
        printf("[AI] FLIP at (%d,%d)\n", idx / 8, idx % 8);
        return true;
    }

    // ── 優先級 4：隨機移動到空格 ──
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            if (!strstr(board_pieces[r][c], my_color)) continue;
            for (int d = 0; d < 4; d++) {
                int nr = r + dirs[d][0], nc = c + dirs[d][1];
                if (nr < 0 || nr >= 4 || nc < 0 || nc >= 8) continue;
                if (strcmp(board_pieces[nr][nc], "Null") == 0) {
                    snprintf(out_action, buf_size, "%d %d %d %d\n", r, c, nr, nc);
                    printf("[AI] MOVE %s to (%d,%d)\n", board_pieces[r][c], nr, nc);
                    return true;
                }
            }
        }
    }

    printf("[AI] No legal moves found!\n");
    return false;
}

// ============================================================
//  列印棋盤（console 視覺化）
// ============================================================

static void print_board(const char* json) {
    char piece[32];
    printf("\n  +--------+--------+--------+--------+--------+--------+--------+--------+\n");
    for (int r = 0; r < 4; r++) {
        printf("%d |", r);
        for (int c = 0; c < 8; c++) {
            get_piece_at(json, r * 8 + c, piece);
            if (strcmp(piece, "Null") == 0)
                printf("  ----  |");
            else if (strcmp(piece, "Covered") == 0)
                printf(" [????] |");
            else {
                // 縮短名稱以適應格子寬度
                char short_name[9];
                if (strstr(piece, "Red_"))
                    snprintf(short_name, sizeof(short_name), "R_%s", piece + 4);
                else if (strstr(piece, "Black_"))
                    snprintf(short_name, sizeof(short_name), "B_%s", piece + 6);
                else
                    strncpy(short_name, piece, 8);
                short_name[8] = '\0';
                printf(" %-6s |", short_name);
            }
        }
        printf("\n  +--------+--------+--------+--------+--------+--------+--------+--------+\n");
    }
    printf("     0        1        2        3        4        5        6        7\n\n");
}

// ============================================================
//  主程式
// ============================================================

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);  // 關閉緩衝，printf 立即顯示
    srand((unsigned int)time(NULL));

    printf("==============================================\n");
    printf("  Banqi Online AI Client\n");
    printf("  Server: %s:%d\n", SERVER_IP, PORT);
    printf("==============================================\n");

    char board_data[RECV_BUF];
    int  last_total_moves = -1;
    char my_color[10] = "";

    // 1. 連線
    if (init_connection() != 0) {
        printf("[Error] Failed to connect to server. Exiting.\n");
        return 1;
    }

    // 2. 加入房間
    auto_join_room();

    // 3. 判斷角色
    char my_role_ab[4] = "";
    if (strcmp(_assigned_role, "first") == 0)
        strcpy(my_role_ab, "A");
    else if (strcmp(_assigned_role, "second") == 0)
        strcpy(my_role_ab, "B");
    printf("[Main] My role: %s (%s)\n", _assigned_role, my_role_ab);

    // 4. 主遊戲迴圈
    while (1) {
        receive_update(board_data, RECV_BUF);

        if (strlen(board_data) == 0) {
            printf("[Main] Empty response, connection may be lost.\n");
            break;
        }

        // 印出原始伺服器訊息
        printf("\n[Server] %s\n", board_data);

        if (!strstr(board_data, "UPDATE")) {
            // 非 UPDATE 訊息（可能是 WIN/LOSE/DRAW）
            continue;
        }

        // 檢查遊戲狀態
        char state[20];
        get_game_state(board_data, state);

        if (strcmp(state, "finished") == 0) {
            printf("\n====================================\n");
            printf("  Game Finished!\n");
            printf("====================================\n");
            print_board(board_data);
            break;
        }

        if (strcmp(state, "waiting") == 0) {
            printf("[Main] Waiting for game to start...\n");
            continue;
        }

        // state == "playing"
        // 顯示棋盤
        print_board(board_data);

        // 更新我的顏色
        if (my_color[0] == '\0') {
            get_role_color(board_data, my_role_ab, my_color);
            if (strcmp(my_color, "None") != 0) {
                printf("[Main] My color determined: %s\n", my_color);
            } else {
                my_color[0] = '\0';  // 重置
            }
        }

        // 取得目前回合
        int  total_moves     = get_total_moves(board_data);
        char turn_role       = get_current_turn_role(board_data);

        printf("[Main] Turn role: %c, My role: %s, Total moves: %d\n",
               turn_role ? turn_role : '?', my_role_ab, total_moves);

        // 判斷是否輪到自己
        if (turn_role == my_role_ab[0] && total_moves != last_total_moves) {
            printf("\n--- It's my turn! (Move %d) ---\n", total_moves);
            last_total_moves = total_moves;

            char action[64] = "";

            // 如果顏色未定（第一手），先翻牌
            if (my_color[0] == '\0') {
                // 隨機翻一個 Covered 的棋子
                int covered[32], ccount = 0;
                char piece[32];
                for (int i = 0; i < 32; i++) {
                    get_piece_at(board_data, i, piece);
                    if (strcmp(piece, "Covered") == 0)
                        covered[ccount++] = i;
                }
                if (ccount > 0) {
                    int idx = covered[rand() % ccount];
                    snprintf(action, sizeof(action), "%d %d\n", idx / 8, idx % 8);
                    printf("[AI] First flip at (%d,%d)\n", idx / 8, idx % 8);
                }
            } else {
                // 呼叫 AI 決定動作
                if (!ai_decide(board_data, my_color, action, sizeof(action))) {
                    printf("[AI] ERROR: Failed to decide a move!\n");
                    continue;
                }
            }

            if (action[0] != '\0') {
                printf("[Main] Sending action: %s", action);
                Sleep(1500);  // 模擬思考時間
                send_action(action);
            }
        }
    }

    // 5. 清理
    printf("\n[Main] Closing connection...\n");
    close_connection();
    return 0;
}

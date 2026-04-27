// ── 必須先 include raylib，再解除 windows.h 的同名 macro 衝突 ──
// windows.h（透過 network.h 間接引入）有幾個與 Raylib 同名的 WinAPI：
//   CloseWindow(HWND)、DrawText(...)、ShowCursor(BOOL)
// 在這裡統一 undef，確保後續呼叫的是 Raylib 版本。
#include "raylib.h"
#ifdef CloseWindow
#undef CloseWindow
#endif
#ifdef DrawText
#undef DrawText
#endif
#ifdef ShowCursor
#undef ShowCursor
#endif

#include "game_state.h"
#include "board.h"
#include "renderer.h"
#include "ai.h"
#include "network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>  // Sleep()

// ============================================================
//  main.c — 主程式入口
//
//  遊戲狀態機：
//
//   STATE_MAIN_MENU
//       │ KEY_1 → 本地模式
//       │ KEY_2 → 線上模式
//       ▼
//   STATE_HAND_MENU (本地)    STATE_CONNECTING (線上)
//       │ KEY_1/2                   │ console 輸入 JOIN
//       ▼                           ▼
//   STATE_PLAYING ──────────────────┘
//       │ 帥/將被吃
//       ▼
//   STATE_RED_WIN / STATE_BLACK_WIN
//       │ KEY_R
//       ▼
//   STATE_MAIN_MENU
//
//  執行緒：
//    主執行緒 — Raylib 遊戲迴圈（繪圖 + 輸入 + 本地 AI）
//    背景執行緒（線上模式）— network.c 的 recv_thread_func
// ============================================================

#define SCREEN_W 800
#define SCREEN_H 450

// ── 全域共享狀態 ──────────────────────────────────────────────
static SharedState gs;
static Renderer    renderer;

// ── 線上模式輔助：console 輸入房間號碼 ───────────────────────
// 因為 Raylib 視窗無法方便地輸入文字，
// 線上模式的「輸入房號」改在 console 完成（另開執行緒讓 Raylib 不卡住）

static char   _pending_room_id[32] = "";
static volatile bool _room_input_done = false;

static DWORD WINAPI room_input_thread(LPVOID param) {
    (void)param;
    char buf[64];
    printf("\n====================================\n");
    printf("[線上模式] 請輸入要加入的房間號碼：");
    fflush(stdout);
    if (fgets(buf, sizeof(buf), stdin)) {
        buf[strcspn(buf, "\r\n")] = '\0';
        strncpy(_pending_room_id, buf, sizeof(_pending_room_id) - 1);
    }
    _room_input_done = true;
    return 0;
}

// ── 線上模式加入房間（在 console 執行緒完成後觸發）────────────
static void do_join_room(void) {
    char role[16] = "";
    printf("[Main] Joining room: %s\n", _pending_room_id);

    if (network_join_room(_pending_room_id, role, sizeof(role)) != 0) {
        printf("[Main] Failed to join room. Returning to main menu.\n");
        gs.state = STATE_MAIN_MENU;
        return;
    }

    // 設定角色
    strncpy(gs.roomId, _pending_room_id, sizeof(gs.roomId) - 1);
    if (strcmp(role, "first") == 0) {
        gs.serverRole = ROLE_FIRST;
        strcpy(gs.myRoleAB, "A");
    } else {
        gs.serverRole = ROLE_SECOND;
        strcpy(gs.myRoleAB, "B");
    }
    gs.myColor[0]  = '\0';   // 等翻牌後才知道顏色
    gs.oppColor[0] = '\0';
    gs.lastTotalMoves = -1;
    gs.isMyTurn = false;

    network_start_recv_thread();
    gs.state = STATE_WAITING;
    printf("[Main] Waiting for game to start...\n");
}

// ── 線上模式：處理一條伺服器 UPDATE ────────────────────────────
static void process_server_update(const char* json) {
    // 解析 state
    char state_str[20];
    board_get_game_state_str(json, state_str);

    if (strcmp(state_str, "waiting") == 0) {
        gs.state = STATE_WAITING;
        return;
    }

    if (strcmp(state_str, "playing") == 0 || strcmp(state_str, "finished") == 0) {
        // 更新棋盤
        board_parse_json(&gs, json);

        // 更新我的顏色（第一手翻牌後才有）
        if (gs.myColor[0] == '\0') {
            char color[10];
            board_get_role_color(json, gs.myRoleAB, color);
            if (strcmp(color, "None") != 0) {
                strncpy(gs.myColor, color, sizeof(gs.myColor) - 1);
                strcpy(gs.oppColor, strcmp(color, "Red") == 0 ? "Black" : "Red");
                printf("[Main] My color: %s\n", gs.myColor);
            }
        }

        // 判斷是否輪到自己
        int total_moves = board_get_total_moves(json);
        char turn_role  = board_get_current_turn_role(json);
        bool my_turn    = (turn_role == gs.myRoleAB[0]);

        gs.isMyTurn = my_turn;

        if (strcmp(state_str, "playing") == 0) {
            gs.state = STATE_PLAYING;
        }

        // 如果是我的回合且 total_moves 變化了，執行 AI 動作
        if (my_turn && total_moves != gs.lastTotalMoves) {
            printf("\n--- My Turn! (Move %d) ---\n", total_moves);
            gs.lastTotalMoves = total_moves;

            // 如果顏色未定（第一手），先翻牌
            if (gs.myColor[0] == '\0') {
                // 找任意一個 Covered 格子翻
                char piece_name[32];
                for (int i = 0; i < 32; i++) {
                    board_get_piece_at(json, i, piece_name);
                    if (strcmp(piece_name, "Covered") == 0) {
                        char action[32];
                        snprintf(action, sizeof(action), "%d %d\n", i / 8, i % 8);
                        printf("[Main] First flip at index %d\n", i);
                        Sleep(1500);
                        network_send_action(action);
                        return;
                    }
                }
            }

            // 呼叫 AI 決定動作
            if (gs.myColor[0] != '\0') {
                char action[64] = "";
                if (server_ai_decide(json, gs.myColor, action, sizeof(action))) {
                    Sleep(1500);  // 模擬思考時間
                    network_send_action(action);
                } else {
                    printf("[Main] AI failed to decide a move!\n");
                }
            }
        }

        // 勝負判定（本地檢查一次）
        board_check_win(&gs);
    }
}

// ── 本地模式：玩家滑鼠操作 ────────────────────────────────────
static void handle_local_player_click(Vector2 mouse) {
    int row, col;
    if (!renderer_screen_to_grid(&renderer, mouse, &row, &col)) return;

    Piece* target = &gs.board[row][col];

    if (gs.selectedRow == -1) {
        // 未選中狀態
        if (!target->isEmpty && !target->isFlipped) {
            // 翻牌
            target->isFlipped = true;
            if (gs.humanColor == -1) {
                gs.humanColor    = (int)target->color;
                gs.computerColor = 1 - gs.humanColor;
                gs.currentTurn   = gs.computerColor;
            } else {
                gs.currentTurn = 1 - gs.currentTurn;
            }
            gs.isHumanTurn = false;
        } else if (!target->isEmpty && target->isFlipped &&
                   (int)target->color == gs.humanColor) {
            // 選中己方棋子
            gs.selectedRow = row;
            gs.selectedCol = col;
        }
    } else {
        // 已選中狀態
        if (row == gs.selectedRow && col == gs.selectedCol) {
            // 取消選取
            gs.selectedRow = -1;
            gs.selectedCol = -1;
        } else if (!target->isEmpty && target->isFlipped &&
                   (int)target->color == gs.humanColor) {
            // 換選另一個己方棋子
            gs.selectedRow = row;
            gs.selectedCol = col;
        } else if (board_is_valid_move(&gs, gs.selectedRow, gs.selectedCol, row, col)) {
            // 執行移動/吃子
            gs.board[row][col] = gs.board[gs.selectedRow][gs.selectedCol];
            gs.board[gs.selectedRow][gs.selectedCol].isEmpty = true;
            gs.selectedRow = -1;
            gs.selectedCol = -1;
            gs.currentTurn = 1 - gs.currentTurn;
            gs.isHumanTurn = false;
        }
    }
    board_check_win(&gs);
}

// ── 重置為初始狀態 ─────────────────────────────────────────────
static void reset_game(void) {
    // 線上模式的連線不在這裡重置（只重置本地遊戲狀態）
    if (gs.mode == MODE_ONLINE) {
        network_close();
    }
    memset(&gs, 0, sizeof(gs));
    gs.state       = STATE_MAIN_MENU;
    gs.selectedRow = -1;
    gs.selectedCol = -1;
    gs.humanColor  = -1;
    gs.computerColor = -1;
    gs.currentTurn = -1;
    gs.isHumanTurn = true;
    gs.lastTotalMoves = -1;
    _pending_room_id[0] = '\0';
    _room_input_done    = false;
}

// ── main ──────────────────────────────────────────────────────
int main(void) {
    srand((unsigned int)time(NULL));

    // 初始化 SharedState
    memset(&gs, 0, sizeof(gs));
    gs.state          = STATE_MAIN_MENU;
    gs.selectedRow    = -1;
    gs.selectedCol    = -1;
    gs.humanColor     = -1;
    gs.computerColor  = -1;
    gs.currentTurn    = -1;
    gs.isHumanTurn    = true;
    gs.lastTotalMoves = -1;

    // 初始化 Raylib
    InitWindow(SCREEN_W, SCREEN_H, "暗棋 Dark Chess");
    SetTargetFPS(60);

    if (renderer_init(&renderer, SCREEN_W, SCREEN_H) != 0) {
        CloseWindow();
        return 1;
    }

    // ── 主遊戲迴圈 ───────────────────────────────────────────
    while (!WindowShouldClose()) {

        // ========================================================
        //  UPDATE
        // ========================================================

        switch (gs.state) {

        // ── 主選單 ──
        case STATE_MAIN_MENU:
            if (IsKeyPressed(KEY_ONE)) {
                gs.mode  = MODE_LOCAL;
                gs.state = STATE_HAND_MENU;
            } else if (IsKeyPressed(KEY_TWO)) {
                gs.mode  = MODE_ONLINE;
                gs.state = STATE_CONNECTING;
                // 初始化網路（非阻塞，畫面會繼續跑）
                printf("[Main] Connecting to server...\n");
                if (network_init() != 0) {
                    printf("[Main] Server connection failed. Back to menu.\n");
                    gs.state = STATE_MAIN_MENU;
                } else {
                    // 開 console 執行緒讓使用者輸入房號
                    _room_input_done = false;
                    HANDLE t = CreateThread(NULL, 0, room_input_thread, NULL, 0, NULL);
                    CloseHandle(t);
                }
            }
            break;

        // ── 先後手選單（本地模式）──
        case STATE_HAND_MENU:
            if (IsKeyPressed(KEY_ONE)) {
                // 玩家先手：人先翻牌
                board_init_and_shuffle(&gs);
                gs.isHumanTurn = true;
                gs.state       = STATE_PLAYING;
            } else if (IsKeyPressed(KEY_TWO)) {
                // 電腦先手：AI 先翻牌
                board_init_and_shuffle(&gs);
                gs.isHumanTurn = false;
                gs.state       = STATE_PLAYING;
            }
            break;

        // ── 連線中（等待 console 輸入完成）──
        case STATE_CONNECTING:
            if (_room_input_done) {
                _room_input_done = false;
                do_join_room();
            }
            break;

        // ── 等待對手 ──
        case STATE_WAITING: {
            char update_buf[8192];
            if (network_poll(update_buf, sizeof(update_buf))) {
                process_server_update(update_buf);
            }
            break;
        }

        // ── 遊玩中 ──
        case STATE_PLAYING:

            if (gs.mode == MODE_LOCAL) {
                // ── 本地模式 ──
                if (gs.isHumanTurn) {
                    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                        handle_local_player_click(GetMousePosition());
                    }
                } else {
                    // 電腦 AI（加計時器增加真實感）
                    gs.aiTimer++;
                    if (gs.aiTimer > 40) {
                        local_ai_move(&gs);
                        if (gs.humanColor != -1) gs.currentTurn = 1 - gs.currentTurn;
                        gs.isHumanTurn = true;
                        gs.aiTimer = 0;
                        board_check_win(&gs);
                    }
                }

            } else {
                // ── 線上模式 ──
                // 1. 輪詢背景執行緒有無新訊息
                char update_buf[8192];
                if (network_poll(update_buf, sizeof(update_buf))) {
                    process_server_update(update_buf);
                }

                // 2. 玩家手動操作（如果這個 client 想讓玩家而非 AI 操作）
                //    目前設計：線上模式全交給 AI（process_server_update 內部呼叫）
                //    若要改成玩家手動操作線上模式，解除下面的注釋並移除 AI 呼叫
                /*
                if (gs.isMyTurn && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    handle_online_player_click(GetMousePosition(), last_json);
                }
                */
            }
            break;

        // ── 勝負畫面 ──
        case STATE_RED_WIN:
        case STATE_BLACK_WIN:
            if (IsKeyPressed(KEY_R)) {
                reset_game();
            }
            break;

        default:
            break;
        }

        // ========================================================
        //  DRAW
        // ========================================================
        BeginDrawing();
        ClearBackground(RAYWHITE);

        switch (gs.state) {
        case STATE_MAIN_MENU:
            renderer_draw_main_menu(&renderer, SCREEN_W, SCREEN_H);
            break;
        case STATE_HAND_MENU:
            renderer_draw_hand_menu(&renderer, SCREEN_W, SCREEN_H);
            break;
        case STATE_CONNECTING:
            renderer_draw_connecting(&renderer, SCREEN_W, SCREEN_H, _pending_room_id);
            break;
        case STATE_WAITING:
            renderer_draw_waiting(&renderer, SCREEN_W, SCREEN_H);
            break;
        case STATE_PLAYING:
            renderer_draw_playing(&renderer, &gs, SCREEN_W, SCREEN_H);
            break;
        case STATE_RED_WIN:
        case STATE_BLACK_WIN:
            renderer_draw_playing(&renderer, &gs, SCREEN_W, SCREEN_H);
            renderer_draw_result(&renderer, &gs, SCREEN_W, SCREEN_H);
            break;
        default:
            break;
        }

        EndDrawing();
    }

    // ── 清理 ──
    renderer_unload(&renderer);
    if (gs.mode == MODE_ONLINE) {
        network_close();
    }
    CloseWindow();
    return 0;
}
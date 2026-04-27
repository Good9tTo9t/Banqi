#ifndef RENDERER_H
#define RENDERER_H

#include "game_state.h"
#include "raylib.h"

// ============================================================
//  renderer.h — Raylib 繪圖模組
//
//  職責：
//    - 載入/卸載材質與字型
//    - 根據 SharedState 繪製當前畫面
//    - 提供滑鼠位置 → 棋盤格子的座標轉換
// ============================================================

// 所有繪圖用的資源打包成一個結構
typedef struct {
    Texture2D boardTex;
    Texture2D pieceBack;
    Texture2D pieceRed;
    Texture2D pieceBlack;
    Font      font;

    // 棋盤位置計算（由 renderer_init 填入）
    float boardX, boardY;   // 棋盤左上角在螢幕的座標
    float startX, startY;   // 第一格左上角相對於棋盤左上角的偏移
    float rightMargin;
    float bottomMargin;
    float cellW, cellH;     // 每格寬高
} Renderer;

// 初始化：載入所有材質與字型，計算格子尺寸
// screenWidth / screenHeight: 視窗大小
// 回傳 0 成功，-1 失敗
int renderer_init(Renderer* r, int screenWidth, int screenHeight);

// 卸載所有材質與字型
void renderer_unload(Renderer* r);

// 將螢幕座標轉為棋盤格子 (row, col)
// 若點擊在棋盤外則回傳 false
bool renderer_screen_to_grid(const Renderer* r, Vector2 mouse, int* out_row, int* out_col);

// ── 各畫面的繪圖函數 ──

// 主選單（選擇模式）
void renderer_draw_main_menu(const Renderer* r, int screenWidth, int screenHeight);

// 先後手選單（本地模式）
void renderer_draw_hand_menu(const Renderer* r, int screenWidth, int screenHeight);

// 連線中畫面
void renderer_draw_connecting(const Renderer* r, int screenWidth, int screenHeight,
                               const char* room_id);

// 等待對手
void renderer_draw_waiting(const Renderer* r, int screenWidth, int screenHeight);

// 遊玩中（棋盤 + UI）
void renderer_draw_playing(const Renderer* r, const SharedState* gs,
                            int screenWidth, int screenHeight);

// 勝負結果畫面（疊加在棋盤上）
void renderer_draw_result(const Renderer* r, const SharedState* gs,
                           int screenWidth, int screenHeight);

#endif // RENDERER_H
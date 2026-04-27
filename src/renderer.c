#include "renderer.h"
#include "board.h"     // RED_NAMES, BLACK_NAMES
#include <stdio.h>
#include <string.h>

// ============================================================
//  renderer.c — Raylib 繪圖實作
// ============================================================

// 字型需要的所有字元（含中文）
#define FONT_CHARS \
    "帥仕相俥傌炮兵將士象車馬砲卒" \
    "請選擇模式先後手：12.玩家電腦翻第一張牌輪到你了思考中..." \
    "你的陣營未定紅黑方獲勝按下R鍵重新開始" \
    "線上對戰本地單機連接房間號碼輸入等待對手加入中斷線" \
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 :"

int renderer_init(Renderer* r, int screenWidth, int screenHeight) {
    r->boardTex  = LoadTexture("texture/board.png");
    r->pieceBack = LoadTexture("texture/piece_back.png");
    r->pieceRed  = LoadTexture("texture/piece_red.png");
    r->pieceBlack= LoadTexture("texture/piece_black.png");

    if (r->boardTex.id == 0 || r->pieceBack.id == 0 ||
        r->pieceRed.id == 0 || r->pieceBlack.id == 0) {
        TraceLog(LOG_ERROR, "renderer_init: Failed to load textures");
        return -1;
    }

    int cpCount = 0;
    int* cps = LoadCodepoints(FONT_CHARS, &cpCount);
    r->font = LoadFontEx("texture/LXGWWenKaiTC-Bold.ttf", 50, cps, cpCount);
    UnloadCodepoints(cps);

    if (r->font.texture.id == 0) {
        TraceLog(LOG_ERROR, "renderer_init: Failed to load font");
        return -1;
    }

    // 計算棋盤位置
    r->boardX = screenWidth  / 2.0f - r->boardTex.width  / 2.0f;
    r->boardY = screenHeight / 2.0f - r->boardTex.height / 2.0f;

    // 這些偏移對應原始 main.c 的 sx, sy, rm, bm
    r->startX      = 62.0f;
    r->startY      = 89.0f;
    r->rightMargin = 62.0f;
    r->bottomMargin= 18.0f;

    r->cellW = (r->boardTex.width  - r->startX - r->rightMargin)  / 8.0f;
    r->cellH = (r->boardTex.height - r->startY - r->bottomMargin) / 4.0f;

    return 0;
}

void renderer_unload(Renderer* r) {
    UnloadTexture(r->boardTex);
    UnloadTexture(r->pieceBack);
    UnloadTexture(r->pieceRed);
    UnloadTexture(r->pieceBlack);
    UnloadFont(r->font);
}

bool renderer_screen_to_grid(const Renderer* r, Vector2 mouse, int* out_row, int* out_col) {
    float gx = r->boardX + r->startX;
    float gy = r->boardY + r->startY;
    float gw = r->boardTex.width  - r->startX - r->rightMargin;
    float gh = r->boardTex.height - r->startY - r->bottomMargin;

    if (mouse.x < gx || mouse.x > gx + gw ||
        mouse.y < gy || mouse.y > gy + gh) return false;

    *out_col = (int)((mouse.x - gx) / r->cellW);
    *out_row = (int)((mouse.y - gy) / r->cellH);

    // 邊界保護
    if (*out_col < 0) *out_col = 0;
    if (*out_col > 7) *out_col = 7;
    if (*out_row < 0) *out_row = 0;
    if (*out_row > 3) *out_row = 3;
    return true;
}

// ── 各畫面繪製 ───────────────────────────────────────────────

void renderer_draw_main_menu(const Renderer* r, int screenWidth, int screenHeight) {
    (void)r;
    DrawRectangle(0, 0, screenWidth, screenHeight, Fade(DARKGRAY, 0.92f));
    DrawTextEx(r->font, "請選擇模式：",
               (Vector2){(float)screenWidth/2 - 120, 140}, 38, 1, WHITE);
    DrawTextEx(r->font, "1. 本地單機（對 AI）",
               (Vector2){(float)screenWidth/2 - 160, 210}, 28, 1, LIGHTGRAY);
    DrawTextEx(r->font, "2. 線上對戰（連伺服器）",
               (Vector2){(float)screenWidth/2 - 180, 260}, 28, 1, LIGHTGRAY);
}

void renderer_draw_hand_menu(const Renderer* r, int screenWidth, int screenHeight) {
    (void)r;
    DrawRectangle(0, 0, screenWidth, screenHeight, Fade(DARKGRAY, 0.92f));
    DrawTextEx(r->font, "請選擇先後手：",
               (Vector2){(float)screenWidth/2 - 120, 150}, 38, 1, WHITE);
    DrawTextEx(r->font, "1. 玩家先手",
               (Vector2){(float)screenWidth/2 - 100, 230}, 30, 1, LIGHTGRAY);
    DrawTextEx(r->font, "2. 電腦先手",
               (Vector2){(float)screenWidth/2 - 100, 280}, 30, 1, LIGHTGRAY);
}

// 簡易 UI：顯示等待連線畫面（含房間輸入提示）
void renderer_draw_connecting(const Renderer* r, int screenWidth, int screenHeight,
                               const char* room_id) {
    (void)r;
    DrawRectangle(0, 0, screenWidth, screenHeight, Fade(DARKGRAY, 0.92f));
    DrawTextEx(r->font, "連接房間中...",
               (Vector2){(float)screenWidth/2 - 120, 150}, 36, 1, YELLOW);

    char info[64];
    snprintf(info, sizeof(info), "房間號碼：%s", room_id[0] ? room_id : "（請至 console 輸入）");
    DrawTextEx(r->font, info,
               (Vector2){(float)screenWidth/2 - 150, 220}, 26, 1, WHITE);
    DrawTextEx(r->font, "請看 console 視窗操作",
               (Vector2){(float)screenWidth/2 - 150, 280}, 22, 1, LIGHTGRAY);
}

void renderer_draw_waiting(const Renderer* r, int screenWidth, int screenHeight) {
    (void)r;
    DrawRectangle(0, 0, screenWidth, screenHeight, Fade(DARKGRAY, 0.92f));
    DrawTextEx(r->font, "等待對手加入...",
               (Vector2){(float)screenWidth/2 - 130, 200}, 36, 1, YELLOW);
}

void renderer_draw_playing(const Renderer* r, const SharedState* gs,
                            int screenWidth, int screenHeight) {
    (void)screenWidth; (void)screenHeight;

    // 棋盤底圖
    DrawTexture(r->boardTex, (int)r->boardX, (int)r->boardY, WHITE);

    // 棋子
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 8; col++) {
            Piece p = gs->board[row][col];
            if (p.isEmpty) continue;

            float px = r->boardX + r->startX + col * r->cellW +
                       r->cellW / 2.0f - r->pieceBack.width  / 2.0f;
            float py = r->boardY + r->startY + row * r->cellH +
                       r->cellH / 2.0f - r->pieceBack.height / 2.0f;

            if (!p.isFlipped) {
                DrawTexture(r->pieceBack, (int)px, (int)py, WHITE);
            } else {
                Texture2D tex = (p.color == PIECE_RED) ? r->pieceRed : r->pieceBlack;
                DrawTexture(tex, (int)px, (int)py, WHITE);

                const char* name = (p.color == PIECE_RED)
                                    ? RED_NAMES[p.role]
                                    : BLACK_NAMES[p.role];
                Vector2 tsz = MeasureTextEx(r->font, name, 50, 1);
                Color   tc  = (p.color == PIECE_RED) ? MAROON : BLACK;
                DrawTextEx(r->font, name,
                           (Vector2){ px + tex.width  / 2.0f - tsz.x / 2.0f,
                                      py + tex.height / 2.0f - tsz.y / 2.0f },
                           50, 1, tc);
            }
        }
    }

    // 選取高亮
    if (gs->selectedRow >= 0) {
        DrawRectangleLinesEx(
            (Rectangle){
                r->boardX + r->startX + gs->selectedCol * r->cellW,
                r->boardY + r->startY + gs->selectedRow * r->cellH,
                r->cellW, r->cellH
            }, 4, GOLD);
    }

    // UI 狀態面板（左上角）
    DrawRectangle(10, 10, 270, 95, Fade(LIGHTGRAY, 0.8f));

    // 回合提示
    const char* turnText;
    if (gs->mode == MODE_LOCAL) {
        if (gs->humanColor == -1)
            turnText = "請翻第一張牌";
        else
            turnText = gs->isHumanTurn ? "輪到你了" : "電腦思考中...";
    } else {
        // 線上模式
        turnText = gs->isMyTurn ? "輪到你了" : "等待對手...";
    }
    Color turnColor = (gs->mode == MODE_LOCAL ? gs->isHumanTurn : gs->isMyTurn)
                      ? BLUE : DARKGRAY;
    DrawTextEx(r->font, turnText, (Vector2){20, 15}, 24, 1, turnColor);

    // 陣營提示
    const char* colorText;
    Color colorCol;
    if (gs->mode == MODE_LOCAL) {
        if (gs->humanColor == -1) {
            colorText = "你的陣營：未定";
            colorCol  = BLACK;
        } else if (gs->humanColor == PIECE_RED) {
            colorText = "你的陣營：紅方";
            colorCol  = RED;
        } else {
            colorText = "你的陣營：黑方";
            colorCol  = BLACK;
        }
    } else {
        // 線上模式
        if (gs->myColor[0] == '\0') {
            colorText = "你的陣營：未定";
            colorCol  = BLACK;
        } else if (strcmp(gs->myColor, "Red") == 0) {
            colorText = "你的陣營：紅方";
            colorCol  = RED;
        } else {
            colorText = "你的陣營：黑方";
            colorCol  = BLACK;
        }
    }
    DrawTextEx(r->font, colorText, (Vector2){20, 50}, 20, 1, colorCol);

    // 線上模式：顯示角色與房間
    if (gs->mode == MODE_ONLINE) {
        char info[64];
        snprintf(info, sizeof(info), "房間：%s  角色：%s", gs->roomId, gs->myRoleAB);
        DrawTextEx(r->font, info, (Vector2){20, 75}, 16, 1, DARKGRAY);
    }
}

void renderer_draw_result(const Renderer* r, const SharedState* gs,
                           int screenWidth, int screenHeight) {
    DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.6f));

    const char* msg = (gs->state == STATE_RED_WIN) ? "紅方獲勝！" : "黑方獲勝！";
    Vector2 sz = MeasureTextEx(r->font, msg, 40, 1);
    DrawTextEx(r->font, msg,
               (Vector2){screenWidth / 2.0f - sz.x / 2, screenHeight / 2.0f - 30},
               40, 1, YELLOW);

    const char* restart = "按下 R 鍵重新開始";
    Vector2 rsz = MeasureTextEx(r->font, restart, 22, 1);
    DrawTextEx(r->font, restart,
               (Vector2){screenWidth / 2.0f - rsz.x / 2, screenHeight / 2.0f + 30},
               22, 1, RAYWHITE);
}
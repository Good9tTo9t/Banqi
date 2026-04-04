#include "raylib.h"
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>

typedef enum { PIECE_RED, PIECE_BLACK } PieceColor;
typedef enum { GENERAL, ADVISOR, ELEPHANT, CHARIOT, HORSE, CANNON, SOLDIER } Role;
// 新增了 STATE_MENU 用來讓玩家選擇先手或後手
typedef enum { STATE_MENU, STATE_PLAYING, STATE_RED_WIN, STATE_BLACK_WIN } GameState;

typedef struct Piece {
    PieceColor color;
    Role role;
    bool isFlipped;
    bool isEmpty;
} Piece;

const char* RED_NAMES[] = { "帥", "仕", "相", "俥", "傌", "炮", "兵" };
const char* BLACK_NAMES[] = { "將", "士", "象", "車", "馬", "包", "卒" };

Piece boardGrid[4][8];
GameState gameState = STATE_MENU;

// 遊戲狀態追蹤
int currentTurn = -1; // 0: 紅方回合, 1: 黑方回合
int selectedRow = -1;
int selectedCol = -1;

// 人機對戰新增變數
int humanColor = -1;     // 玩家的陣營 (0或1)，-1代表還沒翻出第一張牌
int computerColor = -1;  // 電腦的陣營
bool isHumanTurn = true; // 現在是不是玩家的操作回合
int aiTimer = 0;         // 電腦思考的延遲計時器

void InitAndShuffleBoard() {
    Piece deck[32];
    int index = 0;
    int counts[7] = { 1, 2, 2, 2, 2, 2, 5 };

    for (int c = 0; c <= 1; c++) {
        for (int r = 0; r < 7; r++) {
            for (int i = 0; i < counts[r]; i++) {
                deck[index].color = (PieceColor)c;
                deck[index].role = (Role)r;
                deck[index].isFlipped = false;
                deck[index].isEmpty = false;
                index++;
            }
        }
    }

    srand((unsigned int)time(NULL));
    for (int i = 31; i > 0; i--) {
        int j = rand() % (i + 1);
        Piece temp = deck[i];
        deck[i] = deck[j];
        deck[j] = temp;
    }

    index = 0;
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 8; col++) {
            boardGrid[row][col] = deck[index++];
        }
    }

    // 重置所有狀態到選單
    gameState = STATE_MENU;
    currentTurn = -1;
    selectedRow = -1;
    selectedCol = -1;
    humanColor = -1;
    computerColor = -1;
    isHumanTurn = true;
    aiTimer = 0;
}

bool IsValidMove(int sr, int sc, int dr, int dc) {
    Piece pSrc = boardGrid[sr][sc];
    Piece pDest = boardGrid[dr][dc];
    if (sr != dr && sc != dc) return false;

    int dist = abs(sr - dr) + abs(sc - dc);

    if (pSrc.role == CANNON) {
        if (pDest.isEmpty) return dist == 1;
        int count = 0;
        if (sr == dr) {
            int minC = (sc < dc) ? sc : dc;
            int maxC = (sc > dc) ? sc : dc;
            for (int c = minC + 1; c < maxC; c++) if (!boardGrid[sr][c].isEmpty) count++;
        }
        else {
            int minR = (sr < dr) ? sr : dr;
            int maxR = (sr > dr) ? sr : dr;
            for (int r = minR + 1; r < maxR; r++) if (!boardGrid[r][sc].isEmpty) count++;
        }
        return (count == 1 && pDest.isFlipped && pDest.color != pSrc.color);
    }

    if (dist != 1) return false;
    if (pDest.isEmpty) return true;
    if (!pDest.isFlipped || pSrc.color == pDest.color) return false;
    if (pSrc.role == GENERAL && pDest.role == SOLDIER) return false;
    if (pSrc.role == SOLDIER && pDest.role == GENERAL) return true;

    return pSrc.role <= pDest.role;
}

void CheckWinCondition() {
    bool redGeneralAlive = false;
    bool blackGeneralAlive = false;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            if (!boardGrid[r][c].isEmpty && boardGrid[r][c].role == GENERAL) {
                if (boardGrid[r][c].color == PIECE_RED) redGeneralAlive = true;
                else blackGeneralAlive = true;
            }
        }
    }
    if (currentTurn != -1) {
        if (!redGeneralAlive) gameState = STATE_BLACK_WIN;
        else if (!blackGeneralAlive) gameState = STATE_RED_WIN;
    }
}

// 幫助函數：檢查某個位置的棋子是否正受到敵人威脅
bool IsUnderThreat(int r, int c) {
    Piece p = boardGrid[r][c];
    for (int er = 0; er < 4; er++) {
        for (int ec = 0; ec < 8; ec++) {
            Piece enemy = boardGrid[er][ec];
            if (!enemy.isEmpty && enemy.isFlipped && enemy.color != p.color) {
                if (IsValidMove(er, ec, r, c)) return true;
            }
        }
    }
    return false;
}

// --- 電腦 AI 邏輯核心 ---
void DoComputerMove() {
    // 條件 6: 遇到會被吃的棋子要避開 (最高優先級)
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            Piece p = boardGrid[r][c];
            if (!p.isEmpty && p.isFlipped && (int)p.color == computerColor) {
                if (IsUnderThreat(r, c)) {
                    // 嘗試尋找任何合法的步數來逃跑 (移動到空格或吃掉敵人)
                    for (int dr = 0; dr < 4; dr++) {
                        for (int dc = 0; dc < 8; dc++) {
                            if (IsValidMove(r, c, dr, dc)) {
                                boardGrid[dr][dc] = boardGrid[r][c];
                                boardGrid[r][c].isEmpty = true;
                                return; // 執行完畢
                            }
                        }
                    }
                }
            }
        }
    }

    // 條件 5: 遇到旁邊可以吃的棋子，吃掉它
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            Piece p = boardGrid[r][c];
            if (!p.isEmpty && p.isFlipped && (int)p.color == computerColor) {
                for (int dr = 0; dr < 4; dr++) {
                    for (int dc = 0; dc < 8; dc++) {
                        Piece target = boardGrid[dr][dc];
                        if (!target.isEmpty && target.isFlipped && target.color != p.color) {
                            if (IsValidMove(r, c, dr, dc)) {
                                boardGrid[dr][dc] = boardGrid[r][c];
                                boardGrid[r][c].isEmpty = true;
                                return;
                            }
                        }
                    }
                }
            }
        }
    }

    // 條件 4: 繼續翻棋子或隨機走動 (如果沒有危險也沒有東西吃)
    int unflippedR[32], unflippedC[32], unflippedCount = 0;
    int moveSrcR[128], moveSrcC[128], moveDstR[128], moveDstC[128], moveCount = 0;

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            Piece p = boardGrid[r][c];
            if (!p.isEmpty && !p.isFlipped) {
                // 收集未翻開的棋子
                unflippedR[unflippedCount] = r;
                unflippedC[unflippedCount] = c;
                unflippedCount++;
            }
            else if (!p.isEmpty && p.isFlipped && (int)p.color == computerColor) {
                // 收集可以走到空格的步數
                for (int dr = 0; dr < 4; dr++) {
                    for (int dc = 0; dc < 8; dc++) {
                        if (boardGrid[dr][dc].isEmpty && IsValidMove(r, c, dr, dc)) {
                            moveSrcR[moveCount] = r; moveSrcC[moveCount] = c;
                            moveDstR[moveCount] = dr; moveDstC[moveCount] = dc;
                            moveCount++;
                        }
                    }
                }
            }
        }
    }

    // 如果電腦還沒決定顏色 (代表它是整場遊戲第一個動的)，它只能翻牌
    if (computerColor == -1 && unflippedCount > 0) {
        int randIdx = rand() % unflippedCount;
        int r = unflippedR[randIdx], c = unflippedC[randIdx];
        boardGrid[r][c].isFlipped = true;
        computerColor = (int)boardGrid[r][c].color;
        humanColor = 1 - computerColor;
        return;
    }

    // 隨機決定要翻牌還是移動
    int totalActions = unflippedCount + moveCount;
    if (totalActions > 0) {
        int randIdx = rand() % totalActions;
        if (randIdx < unflippedCount) { // 抽到翻牌
            int r = unflippedR[randIdx], c = unflippedC[randIdx];
            boardGrid[r][c].isFlipped = true;
            if (humanColor == -1) { // 遊戲第一手
                computerColor = (int)boardGrid[r][c].color;
                humanColor = 1 - computerColor;
            }
        }
        else { // 抽到移動
            int mIdx = randIdx - unflippedCount;
            boardGrid[moveDstR[mIdx]][moveDstC[mIdx]] = boardGrid[moveSrcR[mIdx]][moveSrcC[mIdx]];
            boardGrid[moveSrcR[mIdx]][moveSrcC[mIdx]].isEmpty = true;
        }
    }
}

int main(void) {
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "Banqi - AI Survival");

    Texture2D boardTex = LoadTexture("texture/board.png");
    Texture2D pieceBack = LoadTexture("texture/piece_back.png");
    Texture2D pieceRed = LoadTexture("texture/piece_red.png");
    Texture2D pieceBlack = LoadTexture("texture/piece_black.png");

    // 增加了必須的字元，確保 UI 顯示正常
    int cpCount = 0;
    int* cps = LoadCodepoints("帥仕相俥傌炮兵將士象車馬包卒請選擇先後手：12.玩家電腦翻第一張牌輪到你了思考中...你的陣營未定紅黑方獲勝！按下 R鍵重新開始", &cpCount);
    Font font = LoadFontEx("texture/LXGWWenKaiTC-Bold.ttf", 50, cps, cpCount);
    UnloadCodepoints(cps);

    InitAndShuffleBoard();

    float bx = screenWidth / 2.0f - boardTex.width / 2.0f;
    float by = screenHeight / 2.0f - boardTex.height / 2.0f;
    float sx = 62.0f, sy = 89.0f, rm = 62.0f, bm = 18.0f;
    float cw = (boardTex.width - sx - rm) / 8.0f;
    float ch = (boardTex.height - sy - bm) / 4.0f;

    SetTargetFPS(60);

    while (!WindowShouldClose()) {

        // 狀態：選單 (選擇先後手)
        if (gameState == STATE_MENU) {
            if (IsKeyPressed(KEY_ONE)) {
                isHumanTurn = true;
                gameState = STATE_PLAYING;
            }
            else if (IsKeyPressed(KEY_TWO)) {
                isHumanTurn = false;
                gameState = STATE_PLAYING;
            }
        }
        // 狀態：遊玩中
        else if (gameState == STATE_PLAYING) {

            // 玩家回合邏輯
            if (isHumanTurn) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    Vector2 m = GetMousePosition();
                    if (m.x >= bx + sx && m.x <= bx + boardTex.width - rm && m.y >= by + sy && m.y <= by + boardTex.height - bm) {
                        int col = (int)((m.x - (bx + sx)) / cw);
                        int row = (int)((m.y - (by + sy)) / ch);
                        Piece* target = &boardGrid[row][col];

                        if (selectedRow == -1) {
                            if (!target->isEmpty && !target->isFlipped) {
                                target->isFlipped = true;
                                if (humanColor == -1) { // 玩家翻開第一張牌
                                    humanColor = target->color;
                                    computerColor = 1 - humanColor;
                                    currentTurn = computerColor; // 換電腦
                                }
                                else {
                                    currentTurn = 1 - currentTurn;
                                }
                                isHumanTurn = false; // 玩家操作完，換電腦
                            }
                            else if (!target->isEmpty && target->isFlipped && (int)target->color == humanColor) {
                                selectedRow = row; selectedCol = col;
                            }
                        }
                        else {
                            if (row == selectedRow && col == selectedCol) {
                                selectedRow = -1; selectedCol = -1;
                            }
                            else if (!target->isEmpty && target->isFlipped && (int)target->color == humanColor) {
                                selectedRow = row; selectedCol = col;
                            }
                            else if (IsValidMove(selectedRow, selectedCol, row, col)) {
                                boardGrid[row][col] = boardGrid[selectedRow][selectedCol];
                                boardGrid[selectedRow][selectedCol].isEmpty = true;
                                selectedRow = -1; selectedCol = -1;
                                currentTurn = 1 - currentTurn;
                                isHumanTurn = false; // 玩家移動完，換電腦
                            }
                        }
                        CheckWinCondition();
                    }
                }
            }
            // 電腦回合邏輯 (加入計時器讓電腦有一點思考時間)
            else {
                aiTimer++;
                if (aiTimer > 40) { // 等待約 40 幀 (約 0.6 秒) 後執行移動
                    DoComputerMove();
                    if (humanColor != -1) currentTurn = 1 - currentTurn; // 切換畫面文字
                    isHumanTurn = true; // 換回玩家
                    aiTimer = 0;
                    CheckWinCondition();
                }
            }
        }
        else if (IsKeyPressed(KEY_R)) InitAndShuffleBoard();

        // --- 繪圖部分 ---
        BeginDrawing();
        ClearBackground(RAYWHITE);

        if (gameState == STATE_MENU) {
            DrawRectangle(0, 0, screenWidth, screenHeight, Fade(DARKGRAY, 0.9f));
            DrawTextEx(font, "請選擇先後手：", (Vector2) { 270, 150 }, 40, 1, WHITE);
            DrawTextEx(font, "1. 玩家先手", (Vector2) { 300, 230 }, 30, 1, LIGHTGRAY);
            DrawTextEx(font, "2. 電腦先手", (Vector2) { 300, 280 }, 30, 1, LIGHTGRAY);
        }
        else {
            DrawTexture(boardTex, bx, by, WHITE);

            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 8; c++) {
                    Piece p = boardGrid[r][c];
                    if (p.isEmpty) continue;
                    float px = bx + sx + (c * cw) + (cw / 2.0f) - (pieceBack.width / 2.0f);
                    float py = by + sy + (r * ch) + (ch / 2.0f) - (pieceBack.height / 2.0f);
                    if (!p.isFlipped) DrawTexture(pieceBack, px, py, WHITE);
                    else {
                        DrawTexture((p.color == PIECE_RED) ? pieceRed : pieceBlack, px, py, WHITE);
                        const char* n = (p.color == PIECE_RED) ? RED_NAMES[p.role] : BLACK_NAMES[p.role];
                        Vector2 v = MeasureTextEx(font, n, 50, 1);
                        DrawTextEx(font, n, (Vector2) { px + pieceRed.width / 2 - v.x / 2, py + pieceRed.height / 2 - v.y / 2 }, 50, 1, (p.color == PIECE_RED) ? MAROON : BLACK);
                    }
                }
            }

            if (selectedRow != -1) DrawRectangleLinesEx((Rectangle) { bx + sx + selectedCol * cw, by + sy + selectedRow * ch, cw, ch }, 4, GOLD);

            // UI 狀態顯示
            DrawRectangle(10, 10, 250, 80, Fade(LIGHTGRAY, 0.8f));

            const char* turnText = (humanColor == -1) ? "請翻第一張牌" : (isHumanTurn ? "輪到你了" : "電腦思考中...");
            DrawTextEx(font, turnText, (Vector2) { 20, 15 }, 24, 1, isHumanTurn ? BLUE : DARKGRAY);

            const char* colorText = (humanColor == -1) ? "你的陣營：未定" : (humanColor == PIECE_RED ? "你的陣營：紅方" : "你的陣營：黑方");
            Color cColor = (humanColor == -1) ? BLACK : (humanColor == PIECE_RED ? RED : BLACK);
            DrawTextEx(font, colorText, (Vector2) { 20, 50 }, 20, 1, cColor);

            if (gameState == STATE_RED_WIN || gameState == STATE_BLACK_WIN) {
                DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.6f));
                const char* w = (gameState == STATE_RED_WIN) ? "紅方獲勝！" : "黑方獲勝！";
                Vector2 ws = MeasureTextEx(font, w, 40, 1);
                DrawTextEx(font, w, (Vector2) { screenWidth / 2 - ws.x / 2, screenHeight / 2 - 20 }, 40, 1, YELLOW);

                const char* restartMsg = "按下 R 鍵重新開始";
                Vector2 textSize = MeasureTextEx(font, restartMsg, 20, 1);
                DrawTextEx(font, restartMsg, (Vector2) { screenWidth / 2 - textSize.x / 2, screenHeight / 2 + 40 }, 20, 1, RAYWHITE);
            }
        }
        EndDrawing();
    }

    UnloadTexture(boardTex); UnloadTexture(pieceBack); UnloadTexture(pieceRed); UnloadTexture(pieceBlack); UnloadFont(font);
    CloseWindow();
    return 0;
}

#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <stdbool.h>

// ============================================================
//  棋子與遊戲狀態的核心資料結構
//  被所有模組共用，只定義資料，不含任何邏輯
// ============================================================

typedef enum {
    PIECE_RED   = 0,
    PIECE_BLACK = 1
} PieceColor;

typedef enum {
    GENERAL  = 0,  // 帥/將  rank 7
    ADVISOR  = 1,  // 仕/士  rank 6
    ELEPHANT = 2,  // 相/象  rank 5
    CHARIOT  = 3,  // 俥/車  rank 4
    HORSE    = 4,  // 傌/馬  rank 3
    CANNON   = 5,  // 炮/砲  rank 2
    SOLDIER  = 6   // 兵/卒  rank 1
} PieceRole;

typedef struct {
    PieceColor color;
    PieceRole  role;
    bool       isFlipped;
    bool       isEmpty;
} Piece;

// 遊戲模式
typedef enum {
    MODE_LOCAL   = 0,  // 本地單機（對 AI）
    MODE_ONLINE  = 1   // 連線模式（對伺服器）
} GameMode;

// 遊戲整體狀態機
typedef enum {
    STATE_MAIN_MENU  = 0,  // 主選單（選模式）
    STATE_HAND_MENU  = 1,  // 先後手選單（本地模式）
    STATE_CONNECTING = 2,  // 連線中
    STATE_WAITING    = 3,  // 等待對手
    STATE_PLAYING    = 4,  // 遊玩中
    STATE_RED_WIN    = 5,
    STATE_BLACK_WIN  = 6
} GameState;

// 連線後伺服器告知的角色
typedef enum {
    ROLE_NONE   = 0,
    ROLE_FIRST  = 1,   // "first" → A
    ROLE_SECOND = 2    // "second" → B
} ServerRole;

// 整場遊戲的共享全域狀態（所有模組透過指標存取）
typedef struct {
    Piece      board[4][8];
    GameState  state;
    GameMode   mode;

    // 當前回合
    int        currentTurn;    // 0=紅, 1=黑, -1=未定
    bool       isMyTurn;       // 線上模式：現在是否輪到自己

    // 陣營資訊
    int        humanColor;     // 本地模式：玩家顏色, -1=未定
    int        computerColor;  // 本地模式：電腦顏色

    // 選取狀態
    int        selectedRow;
    int        selectedCol;

    // 線上模式資訊
    ServerRole serverRole;         // first / second
    char       myRoleAB[4];        // "A" 或 "B"
    char       myColor[10];        // "Red" 或 "Black"
    char       oppColor[10];       // 對手顏色
    int        lastTotalMoves;     // 防重複觸發
    char       roomId[32];

    // AI 延遲計時（本地模式）
    int        aiTimer;
    bool       isHumanTurn;        // 本地模式：現在是玩家回合
} SharedState;

#endif // GAME_STATE_H
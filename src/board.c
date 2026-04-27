#include "board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
//  board.c — 棋盤邏輯實作
// ============================================================

const char* RED_NAMES[7]   = {"帥", "仕", "相", "俥", "傌", "炮", "兵"};
const char* BLACK_NAMES[7] = {"將", "士", "象", "車", "馬", "砲", "卒"};

// ---------- 初始化 ----------

void board_init_and_shuffle(SharedState* gs) {
    Piece pieces[32];
    int idx = 0;
    PieceRole roles[] = {
        GENERAL,
        ADVISOR, ADVISOR,
        ELEPHANT, ELEPHANT,
        CHARIOT, CHARIOT,
        HORSE, HORSE,
        CANNON, CANNON,
        SOLDIER, SOLDIER, SOLDIER, SOLDIER, SOLDIER
    };
    for (int color = 0; color < 2; color++) {
        for (int i = 0; i < 16; i++) {
            pieces[idx].color    = (PieceColor)color;
            pieces[idx].role     = roles[i];
            pieces[idx].isFlipped = false;
            pieces[idx].isEmpty   = false;
            idx++;
        }
    }
    // Fisher-Yates shuffle
    for (int i = 31; i > 0; i--) {
        int j = rand() % (i + 1);
        Piece tmp = pieces[i];
        pieces[i] = pieces[j];
        pieces[j] = tmp;
    }
    for (int i = 0; i < 32; i++)
        gs->board[i / 8][i % 8] = pieces[i];
}

void board_clear(SharedState* gs) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 8; c++)
            gs->board[r][c].isEmpty = true;
}

// ---------- 規則判斷 ----------

int board_get_rank(PieceRole role) {
    switch (role) {
        case GENERAL:  return 7;
        case ADVISOR:  return 6;
        case ELEPHANT: return 5;
        case CHARIOT:  return 4;
        case HORSE:    return 3;
        case CANNON:   return 2;
        case SOLDIER:  return 1;
        default:       return 0;
    }
}

int board_get_rank_by_name(const char* name) {
    if (strstr(name, "King"))     return 7;
    if (strstr(name, "Guard"))    return 6;
    if (strstr(name, "Elephant")) return 5;
    if (strstr(name, "Car"))      return 4;
    if (strstr(name, "Horse"))    return 3;
    if (strstr(name, "Cannon"))   return 2;
    if (strstr(name, "Soldier"))  return 1;
    return 0;
}

static int count_between(SharedState* gs, int r, int c, int tr, int tc) {
    int count = 0;
    if (r == tr) {
        int mn = (c < tc) ? c : tc, mx = (c > tc) ? c : tc;
        for (int ci = mn + 1; ci < mx; ci++)
            if (!gs->board[r][ci].isEmpty) count++;
    } else if (c == tc) {
        int mn = (r < tr) ? r : tr, mx = (r > tr) ? r : tr;
        for (int ri = mn + 1; ri < mx; ri++)
            if (!gs->board[ri][c].isEmpty) count++;
    }
    return count;
}

bool board_is_valid_move(SharedState* gs, int sr, int sc, int dr, int dc) {
    Piece* src = &gs->board[sr][sc];
    Piece* dst = &gs->board[dr][dc];
    if (src->isEmpty || !src->isFlipped) return false;
    if (sr == dr && sc == dc) return false;

    // 炮特殊處理
    if (src->role == CANNON) {
        int dist = abs(sr - dr) + abs(sc - dc);
        if (dist == 1 && dst->isEmpty) return true;          // 移動到相鄰空格
        if (sr != dr && sc != dc) return false;               // 不在同行/列
        if (dst->isEmpty || !dst->isFlipped) return false;
        if (src->color == dst->color) return false;
        return count_between(gs, sr, sc, dr, dc) == 1;       // 跳吃
    }

    // 一般棋子：必須相鄰
    if (abs(sr - dr) + abs(sc - dc) != 1) return false;
    if (dst->isEmpty) return true;
    if (!dst->isFlipped || src->color == dst->color) return false;

    int a = board_get_rank(src->role);
    int v = board_get_rank(dst->role);
    if (a == 1) return (v == 7 || v == 1);  // 兵吃帥/兵
    if (a == 7) return (v != 1);            // 帥不吃兵
    return a >= v;
}

bool board_is_under_threat(SharedState* gs, int r, int c) {
    Piece* me = &gs->board[r][c];
    if (me->isEmpty || !me->isFlipped) return false;

    int dirs[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};
    for (int d = 0; d < 4; d++) {
        int er = r + dirs[d][0], ec = c + dirs[d][1];
        if (er < 0 || er >= 4 || ec < 0 || ec >= 8) continue;
        Piece* enemy = &gs->board[er][ec];
        if (enemy->isEmpty || !enemy->isFlipped || enemy->color == me->color) continue;
        int a = board_get_rank(enemy->role);
        int v = board_get_rank(me->role);
        if (enemy->role == CANNON) continue;  // 炮不能相鄰吃
        if (a == 1 && (v == 7 || v == 1)) return true;
        if (a == 7 && v == 1) continue;
        if (a >= v) return true;
    }
    // 檢查敵方炮跳吃
    for (int er = 0; er < 4; er++) {
        for (int ec = 0; ec < 8; ec++) {
            Piece* enemy = &gs->board[er][ec];
            if (enemy->isEmpty || !enemy->isFlipped) continue;
            if (enemy->color == me->color || enemy->role != CANNON) continue;
            if (er != r && ec != c) continue;
            if (count_between(gs, er, ec, r, c) == 1) return true;
        }
    }
    return false;
}

// ---------- 勝負判定 ----------

void board_check_win(SharedState* gs) {
    if (gs->mode == MODE_ONLINE) return; // 線上模式交給伺服器判定

    bool redAlive = false, blackAlive = false;
    int coveredCount = 0;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            Piece* p = &gs->board[r][c];
            if (!p->isEmpty) {
                if (!p->isFlipped) {
                    coveredCount++;
                } else {
                    if (p->color == PIECE_RED)   redAlive = true;
                    if (p->color == PIECE_BLACK) blackAlive = true;
                }
            }
        }
    }

    // 如果還有未翻開的棋子，任何一方都可以翻牌，遊戲尚未結束
    if (coveredCount > 0) return;

    if (!redAlive)   gs->state = STATE_BLACK_WIN;
    if (!blackAlive) gs->state = STATE_RED_WIN;
}

// ---------- JSON 解析 ----------

void board_get_piece_at(const char* json, int index, char* out_piece) {
    const char* p = strstr(json, "\"board\":");
    if (!p) { strcpy(out_piece, "Unknown"); return; }
    p = strstr(p, "[[");
    if (!p) { strcpy(out_piece, "Unknown"); return; }
    p += 2;
    for (int i = 0; i <= index; i++) {
        p = strchr(p, '\"');
        if (!p) { strcpy(out_piece, "Unknown"); return; }
        p++;
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

void board_get_role_color(const char* json, const char* role, char* out_color) {
    char key[20];
    sprintf(key, "\"%s\": \"", role);
    const char* p = strstr(json, key);
    if (!p) { sprintf(key, "\"%s\":\"", role); p = strstr(json, key); }
    if (p) {
        p += strlen(key);
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

char board_get_current_turn_role(const char* json) {
    const char* p = strstr(json, "\"current_turn_role\":");
    if (!p) return '\0';
    p += 20;  // skip past "current_turn_role":
    // 跳過空白
    while (*p == ' ') p++;
    // 檢查 null
    if (*p == 'n' || *p == 'N') return '\0';
    // 跳過引號
    if (*p == '\"') p++;
    return *p;
}

int board_get_total_moves(const char* json) {
    const char* p = strstr(json, "\"total_moves\":");
    if (!p) return -1;
    p += 14;
    while (*p == ' ') p++;
    return atoi(p);
}

void board_get_game_state_str(const char* json, char* out_state) {
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

void board_name_to_piece(const char* piece_name, Piece* out) {
    memset(out, 0, sizeof(Piece));
    if (strcmp(piece_name, "Null") == 0) {
        out->isEmpty = true;
        return;
    }
    if (strcmp(piece_name, "Covered") == 0) {
        out->isEmpty   = false;
        out->isFlipped = false;
        return;
    }
    out->isEmpty   = false;
    out->isFlipped = true;
    out->color = strstr(piece_name, "Red") ? PIECE_RED : PIECE_BLACK;

    if (strstr(piece_name, "King"))          out->role = GENERAL;
    else if (strstr(piece_name, "Guard"))    out->role = ADVISOR;
    else if (strstr(piece_name, "Elephant")) out->role = ELEPHANT;
    else if (strstr(piece_name, "Car"))      out->role = CHARIOT;
    else if (strstr(piece_name, "Horse"))    out->role = HORSE;
    else if (strstr(piece_name, "Cannon"))   out->role = CANNON;
    else if (strstr(piece_name, "Soldier"))  out->role = SOLDIER;
}

bool board_parse_json(SharedState* gs, const char* json) {
    for (int i = 0; i < 32; i++) {
        char name[32];
        board_get_piece_at(json, i, name);
        if (strcmp(name, "Unknown") == 0) return false;
        board_name_to_piece(name, &gs->board[i / 8][i % 8]);
    }
    return true;
}
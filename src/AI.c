#include "ai.h"
#include "board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
//  ai.c — AI 邏輯實作
// ============================================================

// ── 本地模式 AI ──────────────────────────────────────────────

void local_ai_move(SharedState* gs) {
    int computerColor = gs->computerColor;

    // 優先級 1：逃跑（自己有棋子被威脅時，移動到安全位置）
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            Piece p = gs->board[r][c];
            if (!p.isEmpty && p.isFlipped && (int)p.color == computerColor) {
                if (board_is_under_threat(gs, r, c)) {
                    for (int dr = 0; dr < 4; dr++) {
                        for (int dc = 0; dc < 8; dc++) {
                            if (board_is_valid_move(gs, r, c, dr, dc)) {
                                gs->board[dr][dc] = gs->board[r][c];
                                gs->board[r][c].isEmpty = true;
                                return;
                            }
                        }
                    }
                }
            }
        }
    }

    // 優先級 2：進攻（吃掉相鄰敵方棋子）
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            Piece p = gs->board[r][c];
            if (!p.isEmpty && p.isFlipped && (int)p.color == computerColor) {
                for (int dr = 0; dr < 4; dr++) {
                    for (int dc = 0; dc < 8; dc++) {
                        Piece target = gs->board[dr][dc];
                        if (!target.isEmpty && target.isFlipped && target.color != p.color) {
                            if (board_is_valid_move(gs, r, c, dr, dc)) {
                                gs->board[dr][dc] = gs->board[r][c];
                                gs->board[r][c].isEmpty = true;
                                return;
                            }
                        }
                    }
                }
            }
        }
    }

    // 優先級 3：翻牌 或 隨機移動到空格
    int unflippedR[32], unflippedC[32], unflippedCount = 0;
    int moveSrcR[128], moveSrcC[128], moveDstR[128], moveDstC[128], moveCount = 0;

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            Piece p = gs->board[r][c];
            if (!p.isEmpty && !p.isFlipped) {
                unflippedR[unflippedCount] = r;
                unflippedC[unflippedCount] = c;
                unflippedCount++;
            } else if (!p.isEmpty && p.isFlipped && (int)p.color == computerColor) {
                for (int dr = 0; dr < 4; dr++) {
                    for (int dc = 0; dc < 8; dc++) {
                        if (gs->board[dr][dc].isEmpty && board_is_valid_move(gs, r, c, dr, dc)) {
                            moveSrcR[moveCount] = r; moveSrcC[moveCount] = c;
                            moveDstR[moveCount] = dr; moveDstC[moveCount] = dc;
                            moveCount++;
                        }
                    }
                }
            }
        }
    }

    // 如果電腦顏色未定（第一手），只翻牌
    if (computerColor == -1 && unflippedCount > 0) {
        int idx = rand() % unflippedCount;
        int r = unflippedR[idx], c = unflippedC[idx];
        gs->board[r][c].isFlipped = true;
        gs->computerColor = (int)gs->board[r][c].color;
        gs->humanColor    = 1 - gs->computerColor;
        return;
    }

    int total = unflippedCount + moveCount;
    if (total > 0) {
        int randIdx = rand() % total;
        if (randIdx < unflippedCount) {
            int r = unflippedR[randIdx], c = unflippedC[randIdx];
            gs->board[r][c].isFlipped = true;
            // 第一手決定顏色
            if (gs->humanColor == -1) {
                gs->computerColor = (int)gs->board[r][c].color;
                gs->humanColor    = 1 - gs->computerColor;
            }
        } else {
            int mIdx = randIdx - unflippedCount;
            gs->board[moveDstR[mIdx]][moveDstC[mIdx]] = gs->board[moveSrcR[mIdx]][moveSrcC[mIdx]];
            gs->board[moveSrcR[mIdx]][moveSrcC[mIdx]].isEmpty = true;
        }
    }
}

// ── 伺服器模式 AI ─────────────────────────────────────────────

bool ai_can_capture_by_name(const char* attacker, const char* victim) {
    if (strcmp(victim, "Null") == 0)    return true;
    if (strcmp(victim, "Covered") == 0) return false;

    int a = board_get_rank_by_name(attacker);
    int v = board_get_rank_by_name(victim);

    if (a == 1) return (v == 7 || v == 1);   // 兵可吃帥/兵
    if (a == 7) return (v != 1);             // 帥不可吃兵
    if (a == 2) return false;                // 炮相鄰不能吃（炮的跳吃在下方單獨處理）
    return a >= v;
}

// 炮跳吃：檢查 (r,c) 的炮能否吃 (tr,tc)
static bool cannon_can_jump_capture(const char* board_pieces[4][8],
                                     int r, int c, int tr, int tc,
                                     const char* opp_color) {
    if (r != tr && c != tc) return false;
    // 目標必須是敵方已翻開的棋子
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

bool server_ai_decide(const char* json, const char* my_color,
                       char* out_action, int action_buf_size) {
    // 先把棋盤全部讀出來，方便後面查詢
    char board_pieces[4][8][32];
    char opp_color[10];
    strcpy(opp_color, strcmp(my_color, "Red") == 0 ? "Black" : "Red");

    for (int i = 0; i < 32; i++) {
        board_get_piece_at(json, i, board_pieces[i / 8][i % 8]);
    }

    // 方向向量
    int dirs[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};

    // ── 優先級 1：逃跑（自己棋子被威脅） ──
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            if (!strstr(board_pieces[r][c], my_color)) continue;

            // 檢查是否受威脅
            bool threatened = false;
            for (int d = 0; d < 4 && !threatened; d++) {
                int er = r + dirs[d][0], ec = c + dirs[d][1];
                if (er < 0 || er >= 4 || ec < 0 || ec >= 8) continue;
                if (strstr(board_pieces[er][ec], opp_color)) {
                    if (ai_can_capture_by_name(board_pieces[er][ec], board_pieces[r][c]))
                        threatened = true;
                }
            }

            if (threatened) {
                // 嘗試移動到空格逃跑
                for (int d = 0; d < 4; d++) {
                    int nr = r + dirs[d][0], nc = c + dirs[d][1];
                    if (nr < 0 || nr >= 4 || nc < 0 || nc >= 8) continue;
                    if (strcmp(board_pieces[nr][nc], "Null") == 0) {
                        snprintf(out_action, action_buf_size, "%d %d %d %d\n", r, c, nr, nc);
                        printf("[AI] ESCAPE %s at (%d,%d) -> (%d,%d)\n",
                               board_pieces[r][c], r, c, nr, nc);
                        return true;
                    }
                }
            }
        }
    }

    // ── 優先級 2：吃子（一般棋子相鄰攻擊） ──
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            if (!strstr(board_pieces[r][c], my_color)) continue;
            for (int d = 0; d < 4; d++) {
                int tr = r + dirs[d][0], tc = c + dirs[d][1];
                if (tr < 0 || tr >= 4 || tc < 0 || tc >= 8) continue;
                if (!strstr(board_pieces[tr][tc], opp_color)) continue;
                if (ai_can_capture_by_name(board_pieces[r][c], board_pieces[tr][tc])) {
                    snprintf(out_action, action_buf_size, "%d %d %d %d\n", r, c, tr, tc);
                    printf("[AI] CAPTURE %s with %s\n", board_pieces[tr][tc], board_pieces[r][c]);
                    return true;
                }
            }
        }
    }

    // ── 優先級 2b：炮跳吃 ──
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            // 只對己方炮
            char cannon_name[16];
            snprintf(cannon_name, sizeof(cannon_name), "%s_Cannon", my_color);
            if (strcmp(board_pieces[r][c], cannon_name) != 0) continue;

            // 掃描同行同列的所有敵方棋子
            for (int tr = 0; tr < 4; tr++) {
                for (int tc = 0; tc < 8; tc++) {
                    if (tr == r && tc == c) continue;
                    if (!strstr(board_pieces[tr][tc], opp_color)) continue;
                    // 建立 const char* 二維陣列的指標版本給函數用
                    const char* bp[4][8];
                    for (int i = 0; i < 4; i++)
                        for (int j = 0; j < 8; j++)
                            bp[i][j] = board_pieces[i][j];
                    if (cannon_can_jump_capture(bp, r, c, tr, tc, opp_color)) {
                        snprintf(out_action, action_buf_size, "%d %d %d %d\n", r, c, tr, tc);
                        printf("[AI] CANNON JUMP CAPTURE at (%d,%d)\n", tr, tc);
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
        snprintf(out_action, action_buf_size, "%d %d\n", idx / 8, idx % 8);
        printf("[AI] FLIP index %d (%d,%d)\n", idx, idx / 8, idx % 8);
        return true;
    }

    // ── 優先級 4：隨機移動到空格 ──
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 8; c++) {
            if (!strstr(board_pieces[r][c], my_color)) continue;
            for (int d = 0; d < 4; d++) {
                int tr = r + dirs[d][0], tc = c + dirs[d][1];
                if (tr < 0 || tr >= 4 || tc < 0 || tc >= 8) continue;
                if (strcmp(board_pieces[tr][tc], "Null") == 0) {
                    snprintf(out_action, action_buf_size, "%d %d %d %d\n", r, c, tr, tc);
                    printf("[AI] MOVE %s to (%d,%d)\n", board_pieces[r][c], tr, tc);
                    return true;
                }
            }
        }
    }

    return false;  // 無合法步數（不應發生）
}
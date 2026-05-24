#include "make_move.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// ============================================================
//  make_move.c — Enhanced Banqi AI
//  Compile: gcc main.c make_move.c -o output -lws2_32
// ============================================================

// Provided by main.c
extern void get_piece_at(const char* json, int index, char* out_piece);
extern void send_action(const char* action);

// ============================================================
//  Board cache
// ============================================================

static char g_board[32][32];

static void cache_board(const char* json) {
    for (int i = 0; i < 32; i++)
        get_piece_at(json, i, g_board[i]);
}

// ============================================================
//  Piece rank  — Cannon MUST come before Car
//  ("Cannon" contains substring "Car", so order matters)
// ============================================================

static int piece_rank(const char* name) {
    if (strstr(name, "King"))     return 7;
    if (strstr(name, "Guard"))    return 6;
    if (strstr(name, "Elephant")) return 5;
    if (strstr(name, "Cannon"))   return 2;  // ← before "Car"!
    if (strstr(name, "Car"))      return 4;
    if (strstr(name, "Horse"))    return 3;
    if (strstr(name, "Soldier"))  return 1;
    return 0;
}

// ============================================================
//  Capture legality (adjacent, non-cannon)
// ============================================================

static int can_capture(const char* attacker, const char* victim) {
    if (strcmp(victim, "Null")    == 0) return 1;
    if (strcmp(victim, "Covered") == 0) return 0;

    int a = piece_rank(attacker);
    int v = piece_rank(victim);

    if (a == 1) return (v == 7 || v == 1); // Soldier captures King or Soldier
    if (a == 7) return (v != 1);           // King captures all except Soldier
    if (a == 2) return 0;                  // Cannon cannot capture adjacently
    return a >= v;
}

// ============================================================
//  Cannon jump-capture check
// ============================================================

static int cannon_can_jump(int r, int c, int tr, int tc, const char* opp_color) {
    if (r != tr && c != tc) return 0;
    if (!strstr(g_board[tr * 8 + tc], opp_color)) return 0;

    int count = 0;
    if (r == tr) {
        int mn = (c < tc) ? c : tc, mx = (c < tc) ? tc : c;
        for (int ci = mn + 1; ci < mx; ci++)
            if (strcmp(g_board[r * 8 + ci], "Null") != 0) count++;
    } else {
        int mn = (r < tr) ? r : tr, mx = (r < tr) ? tr : r;
        for (int ri = mn + 1; ri < mx; ri++)
            if (strcmp(g_board[ri * 8 + c], "Null") != 0) count++;
    }
    return count == 1;
}

// ============================================================
//  Threat detection (adjacent + cannon jumps)
// ============================================================

static int is_threatened(int r, int c, const char* opp_color) {
    const char* me = g_board[r * 8 + c];
    int dirs[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};

    for (int d = 0; d < 4; d++) {
        int er = r + dirs[d][0], ec = c + dirs[d][1];
        if (er < 0 || er >= 4 || ec < 0 || ec >= 8) continue;
        const char* e = g_board[er * 8 + ec];
        if (strstr(e, opp_color) && can_capture(e, me)) return 1;
    }

    for (int ec = 0; ec < 8; ec++) {
        if (ec == c) continue;
        const char* e = g_board[r * 8 + ec];
        if (strstr(e, opp_color) && strstr(e, "Cannon") &&
            cannon_can_jump(r, ec, r, c, opp_color)) return 1;
    }
    for (int er = 0; er < 4; er++) {
        if (er == r) continue;
        const char* e = g_board[er * 8 + c];
        if (strstr(e, opp_color) && strstr(e, "Cannon") &&
            cannon_can_jump(er, c, r, c, opp_color)) return 1;
    }

    return 0;
}

// ============================================================
//  Simulate / undo a move on g_board
// ============================================================

static void sim_apply(int fr, int fc, int tr, int tc,
                      char sf[32], char st[32]) {
    strcpy(sf, g_board[fr * 8 + fc]);
    strcpy(st, g_board[tr * 8 + tc]);
    strcpy(g_board[tr * 8 + tc], g_board[fr * 8 + fc]);
    strcpy(g_board[fr * 8 + fc], "Null");
}

static void sim_undo(int fr, int fc, int tr, int tc,
                     const char sf[32], const char st[32]) {
    strcpy(g_board[fr * 8 + fc], sf);
    strcpy(g_board[tr * 8 + tc], st);
}

// ============================================================
//  make_move — called every turn by main.c
// ============================================================

void make_move(const char* json, const char* my_role_ab) {
    char my_color[10], opp_color[10];
    get_role_color(json, my_role_ab, my_color);

    // Color not yet assigned — must flip first
    if (strcmp(my_color, "None") == 0) {
        char action[32];
        sprintf(action, "0 0\n");
        printf("[AI] Initial FLIP\n");
        Sleep(2000);
        send_action(action);
        return;
    }

    strcpy(opp_color, strcmp(my_color, "Red") == 0 ? "Black" : "Red");

    cache_board(json);

    int dirs[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};

    int  best_score       = -999999;
    char best_action[32]  = "";
    char best_desc[128]   = "none";

#define CONSIDER(score, action, desc)            \
    if ((score) > best_score) {                  \
        best_score = (score);                    \
        strncpy(best_action, (action), 31);      \
        best_action[31] = '\0';                  \
        strncpy(best_desc, (desc), 127);         \
        best_desc[127] = '\0';                   \
    }

    for (int i = 0; i < 32; i++) {
        const char* piece = g_board[i];
        int r = i / 8, c = i % 8;

        // ── FLIP ─────────────────────────────────────────────
        if (strcmp(piece, "Covered") == 0) {
            int score = 15;
            for (int d = 0; d < 4; d++) {
                int nr = r + dirs[d][0], nc = c + dirs[d][1];
                if (nr < 0 || nr >= 4 || nc < 0 || nc >= 8) continue;
                const char* nb = g_board[nr * 8 + nc];
                if (strstr(nb, opp_color) && piece_rank(nb) >= 4)
                    score -= 20;
                else if (strstr(nb, my_color))
                    score += 5;
            }
            char action[32], desc[128];
            sprintf(action, "%d %d\n", r, c);
            sprintf(desc, "FLIP (%d,%d) score=%d", r, c, score);
            CONSIDER(score, action, desc);
            continue;
        }

        if (!strstr(piece, my_color)) continue;

        int is_cannon = (strstr(piece, "Cannon") != NULL);

        // ── ADJACENT MOVES (non-cannon) ──────────────────────
        if (!is_cannon) {
            for (int d = 0; d < 4; d++) {
                int tr = r + dirs[d][0], tc = c + dirs[d][1];
                if (tr < 0 || tr >= 4 || tc < 0 || tc >= 8) continue;
                const char* tgt = g_board[tr * 8 + tc];
                char action[32], desc[128];

                if (strstr(tgt, opp_color) && can_capture(piece, tgt)) {
                    // Capture — penalise if we'd be recaptured
                    int score = piece_rank(tgt) * 100;
                    char sf[32], st[32];
                    sim_apply(r, c, tr, tc, sf, st);
                    if (is_threatened(tr, tc, opp_color))
                        score -= piece_rank(piece) * 80;
                    sim_undo(r, c, tr, tc, sf, st);

                    sprintf(action, "%d %d %d %d\n", r, c, tr, tc);
                    sprintf(desc, "CAPTURE %s with %s score=%d", tgt, piece, score);
                    CONSIDER(score, action, desc);

                } else if (strcmp(tgt, "Null") == 0) {
                    // Plain move — reward escape, penalise walking into danger
                    int safe_before = !is_threatened(r, c, opp_color);
                    char sf[32], st[32];
                    sim_apply(r, c, tr, tc, sf, st);
                    int safe_after = !is_threatened(tr, tc, opp_color);
                    sim_undo(r, c, tr, tc, sf, st);

                    int score = 10;
                    if (!safe_before &&  safe_after) score += piece_rank(piece) * 60;
                    if ( safe_before && !safe_after) score += piece_rank(piece) * (-50);

                    sprintf(action, "%d %d %d %d\n", r, c, tr, tc);
                    sprintf(desc, "MOVE %s (%d,%d)->(%d,%d) score=%d",
                            piece, r, c, tr, tc, score);
                    CONSIDER(score, action, desc);
                }
            }
        }

        // ── CANNON MOVES ─────────────────────────────────────
        if (is_cannon) {
            // Jump captures — scan full row and column
            for (int tr = 0; tr < 4; tr++) {
                for (int tc = 0; tc < 8; tc++) {
                    if (tr == r && tc == c) continue;
                    if (!strstr(g_board[tr * 8 + tc], opp_color)) continue;
                    if (!cannon_can_jump(r, c, tr, tc, opp_color)) continue;

                    const char* tgt = g_board[tr * 8 + tc];
                    int score = piece_rank(tgt) * 100;
                    char sf[32], st[32];
                    sim_apply(r, c, tr, tc, sf, st);
                    if (is_threatened(tr, tc, opp_color))
                        score -= piece_rank(piece) * 80;
                    sim_undo(r, c, tr, tc, sf, st);

                    char action[32], desc[128];
                    sprintf(action, "%d %d %d %d\n", r, c, tr, tc);
                    sprintf(desc, "CANNON JUMP CAPTURE %s score=%d", tgt, score);
                    CONSIDER(score, action, desc);
                }
            }

            // Reposition along clear row
            for (int tc = 0; tc < 8; tc++) {
                if (tc == c || strcmp(g_board[r * 8 + tc], "Null") != 0) continue;
                int clear = 1;
                int mn = (c<tc)?c:tc, mx = (c<tc)?tc:c;
                for (int ci = mn+1; ci < mx; ci++)
                    if (strcmp(g_board[r * 8 + ci], "Null") != 0) { clear = 0; break; }
                if (!clear) continue;

                int safe_before = !is_threatened(r, c, opp_color);
                char sf[32], st[32];
                sim_apply(r, c, r, tc, sf, st);
                int safe_after = !is_threatened(r, tc, opp_color);
                sim_undo(r, c, r, tc, sf, st);

                int score = 10;
                if (!safe_before &&  safe_after) score += piece_rank(piece) * 60;
                if ( safe_before && !safe_after) score += piece_rank(piece) * (-50);

                char action[32], desc[128];
                sprintf(action, "%d %d %d %d\n", r, c, r, tc);
                sprintf(desc, "CANNON REPOSITION (%d,%d)->(%d,%d) score=%d",
                        r, c, r, tc, score);
                CONSIDER(score, action, desc);
            }

            // Reposition along clear column
            for (int tr = 0; tr < 4; tr++) {
                if (tr == r || strcmp(g_board[tr * 8 + c], "Null") != 0) continue;
                int clear = 1;
                int mn = (r<tr)?r:tr, mx = (r<tr)?tr:r;
                for (int ri = mn+1; ri < mx; ri++)
                    if (strcmp(g_board[ri * 8 + c], "Null") != 0) { clear = 0; break; }
                if (!clear) continue;

                int safe_before = !is_threatened(r, c, opp_color);
                char sf[32], st[32];
                sim_apply(r, c, tr, c, sf, st);
                int safe_after = !is_threatened(tr, c, opp_color);
                sim_undo(r, c, tr, c, sf, st);

                int score = 10;
                if (!safe_before &&  safe_after) score += piece_rank(piece) * 60;
                if ( safe_before && !safe_after) score += piece_rank(piece) * (-50);

                char action[32], desc[128];
                sprintf(action, "%d %d %d %d\n", r, c, tr, c);
                sprintf(desc, "CANNON REPOSITION (%d,%d)->(%d,%d) score=%d",
                        r, c, tr, c, score);
                CONSIDER(score, action, desc);
            }
        }
    }

    if (best_action[0] != '\0') {
        printf("[AI] %s\n", best_desc);
        Sleep(2000);
        send_action(best_action);
    } else {
        printf("[AI] No legal move found!\n");
    }
}

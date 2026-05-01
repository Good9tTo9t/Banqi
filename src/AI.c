#include "ai.h"
#include "board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
//  ai.c — 2-ply Minimax + 進攻壓力評估
//
//  修正「來回踱步」問題的三個新機制：
//
//  1. 接近獎勵 (Proximity Bonus)
//     移動後離最近敵方棋子的距離縮短 → 加分
//     讓 AI 主動縮短與敵方的距離，而非原地不動
//
//  2. 進攻壓力懲罰 (Stagnation Penalty)
//     pure move（移到空格）但沒有改善壓制或縮短距離 → 扣分
//     直接封死「移到A，下一步再移回來」這種來回行為
//
//  3. 全域進攻傾向 (Aggression Bias)
//     evaluate_board 加入「我方能攻擊到的格子總數」作為獎勵
//     讓佔據攻擊性位置的盤面天生比防守性盤面評分高
// ============================================================

// ── 評分常數 ─────────────────────────────────────────────────
#define SCORE_PROXIMITY_BONUS     25   // 每縮短一格距離的加分
#define SCORE_STAGNATION_PENALTY -35   // 毫無意義的移動懲罰
#define SCORE_ATTACK_FIELD        12   // 每個能攻擊的格子加分（進攻傾向）
#define SCORE_FLIP_COVERED_BONUS  20   // 翻牌本身的固定獎勵（逼進攻）

// ── 棋子價值表 ───────────────────────────────────────────────
static int piece_value(const char* name) {
    if (strstr(name, "King"))     return 1000;
    if (strstr(name, "Guard"))    return  200;
    if (strstr(name, "Elephant")) return  250;
    if (strstr(name, "Car"))      return  400;
    if (strstr(name, "Horse"))    return  300;
    if (strstr(name, "Cannon"))   return  350;
    if (strstr(name, "Soldier"))  return  100;
    return 0;
}

// ============================================================
//  工具函數
// ============================================================

bool ai_can_capture_by_name(const char* attacker, const char* victim) {
    if (strcmp(victim, "Null")    == 0) return true;
    if (strcmp(victim, "Covered") == 0) return false;
    int a = board_get_rank_by_name(attacker);
    int v = board_get_rank_by_name(victim);
    if (a == 1) return (v == 7 || v == 1);
    if (a == 7) return (v != 1);
    if (a == 2) return false;
    return a >= v;
}

static bool cannon_can_jump_capture(const char* bp[4][8],
                                     int r, int c, int tr, int tc,
                                     const char* opp_color) {
    if (r != tr && c != tc) return false;
    if (!strstr(bp[tr][tc], opp_color)) return false;
    int count = 0;
    if (r == tr) {
        int mn=(c<tc)?c:tc, mx=(c>tc)?c:tc;
        for (int ci=mn+1; ci<mx; ci++)
            if (strcmp(bp[r][ci],"Null")!=0) count++;
    } else {
        int mn=(r<tr)?r:tr, mx=(r>tr)?r:tr;
        for (int ri=mn+1; ri<mx; ri++)
            if (strcmp(bp[ri][c],"Null")!=0) count++;
    }
    return count == 1;
}

static bool server_is_threatened(const char* bp[4][8],
                                  int r, int c,
                                  const char* opp_color) {
    const char* me = bp[r][c];
    int dirs[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};
    for (int d=0; d<4; d++) {
        int er=r+dirs[d][0], ec=c+dirs[d][1];
        if (er<0||er>=4||ec<0||ec>=8) continue;
        if (strstr(bp[er][ec], opp_color) &&
            ai_can_capture_by_name(bp[er][ec], me)) return true;
    }
    for (int ec=0; ec<8; ec++) {
        if (ec==c) continue;
        if (strstr(bp[r][ec], opp_color) && strstr(bp[r][ec],"Cannon") &&
            cannon_can_jump_capture(bp, r, ec, r, c, opp_color)) return true;
    }
    for (int er=0; er<4; er++) {
        if (er==r) continue;
        if (strstr(bp[er][c], opp_color) && strstr(bp[er][c],"Cannon") &&
            cannon_can_jump_capture(bp, er, c, r, c, opp_color)) return true;
    }
    return false;
}

static void apply_move(const char* bp[4][8], char tmp[4][8][32],
                        int fr, int fc, int tr, int tc) {
    for (int i=0;i<4;i++) for(int j=0;j<8;j++) {
        strncpy(tmp[i][j], bp[i][j], 31);
        tmp[i][j][31]='\0';
    }
    strncpy(tmp[tr][tc], bp[fr][fc], 31);
    strncpy(tmp[fr][fc], "Null", 31);
}

// ── 曼哈頓距離 ───────────────────────────────────────────────
static int manhattan(int r1, int c1, int r2, int c2) {
    int dr = r1-r2; if(dr<0)dr=-dr;
    int dc = c1-c2; if(dc<0)dc=-dc;
    return dr+dc;
}

// ── 計算某顏色方所有棋子到最近敵方的平均距離 ─────────────────
// 越小表示越接近敵人（越積極）
static int total_distance_to_enemy(const char* bp[4][8],
                                    const char* my_color,
                                    const char* opp_color) {
    int total = 0;
    int count = 0;
    for (int r=0;r<4;r++) for(int c=0;c<8;c++) {
        if (!strstr(bp[r][c], my_color)) continue;
        int min_dist = 99;
        for (int er=0;er<4;er++) for(int ec=0;ec<8;ec++) {
            if (!strstr(bp[er][ec], opp_color)) continue;
            int d = manhattan(r,c,er,ec);
            if (d < min_dist) min_dist = d;
        }
        if (min_dist < 99) { total += min_dist; count++; }
    }
    return count > 0 ? total : 99;
}

// ============================================================
//  盤面靜態評估函數
//  從 my_color 角度評估，分數越高越好
// ============================================================

static int evaluate_board(const char* bp[4][8],
                           const char* my_color,
                           const char* opp_color) {
    int dirs[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};
    int score = 0;

    int covered_count = 0;
    for (int r=0;r<4;r++) for(int c=0;c<8;c++)
        if (strcmp(bp[r][c],"Covered")==0) covered_count++;
    bool late_game = (covered_count < 8);

    for (int r=0;r<4;r++) {
        for (int c=0;c<8;c++) {
            const char* piece = bp[r][c];
            if (strcmp(piece,"Null")==0||strcmp(piece,"Covered")==0) continue;
            bool is_mine = strstr(piece,my_color)!=NULL;
            bool is_opp  = strstr(piece,opp_color)!=NULL;
            if (!is_mine&&!is_opp) continue;

            int pv   = piece_value(piece);
            int sign = is_mine ? 1 : -1;

            // 1. 基礎棋力
            score += sign * pv;

            // 2. 安全性
            bool threatened = server_is_threatened(bp, r, c,
                                is_mine ? opp_color : my_color);
            if (threatened) score -= sign * (pv / 3);

            // 3. 壓制獎勵
            if (is_mine) {
                bool is_cannon = strstr(piece,"Cannon")!=NULL;
                if (!is_cannon) {
                    for (int d=0;d<4;d++) {
                        int nr=r+dirs[d][0], nc=c+dirs[d][1];
                        if (nr<0||nr>=4||nc<0||nc>=8) continue;
                        if (strstr(bp[nr][nc],opp_color) &&
                            ai_can_capture_by_name(piece,bp[nr][nc]))
                            score += piece_value(bp[nr][nc]) / 7;
                    }
                } else {
                    for (int tr=0;tr<4;tr++) for(int tc=0;tc<8;tc++) {
                        if (tr==r&&tc==c) continue;
                        if (!strstr(bp[tr][tc],opp_color)) continue;
                        if (cannon_can_jump_capture(bp,r,c,tr,tc,opp_color))
                            score += piece_value(bp[tr][tc]) / 7;
                    }
                }
            }

            // 4. 機動性（後期）
            if (is_mine && late_game) {
                for (int d=0;d<4;d++) {
                    int nr=r+dirs[d][0], nc=c+dirs[d][1];
                    if (nr<0||nr>=4||nc<0||nc>=8) continue;
                    if (strcmp(bp[nr][nc],"Null")==0 ||
                        (strstr(bp[nr][nc],opp_color) &&
                         ai_can_capture_by_name(piece,bp[nr][nc])))
                        score += SCORE_ATTACK_FIELD;
                }
            }

            // 5. 帥的位置懲罰（後期角落）
            if (is_mine && late_game && strstr(piece,"King")) {
                if (r==0||r==3||c==0||c==7) score -= 50;
            }
        }
    }

    // ★ 新增：進攻傾向 — 距離越近分數越高
    // 我方到敵方平均距離越小 → 加分（表示在積極逼近）
    // 用最大可能距離 10（4行8列的最大曼哈頓距離=3+7=10）減去實際距離
    int my_dist  = total_distance_to_enemy(bp, my_color,  opp_color);
    int opp_dist = total_distance_to_enemy(bp, opp_color, my_color);
    // 我方比對方更靠近 → 加分；對方更靠近我 → 扣分
    score += (opp_dist - my_dist) * SCORE_PROXIMITY_BONUS;

    return score;
}

// ============================================================
//  動作結構 & 動作收集
// ============================================================

typedef struct {
    int fr, fc, tr, tc;
    bool is_flip;
} Move;

#define MAX_MOVES 200

static int collect_moves(const char* bp[4][8],
                          const char* my_color,
                          const char* opp_color,
                          Move* moves, int max_moves) {
    int dirs[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};
    int count = 0;

    for (int r=0;r<4&&count<max_moves;r++) {
        for (int c=0;c<8&&count<max_moves;c++) {
            const char* piece = bp[r][c];

            if (strcmp(piece,"Covered")==0) {
                moves[count++] = (Move){r,c,r,c,true};
                continue;
            }
            if (!strstr(piece,my_color)) continue;

            bool is_cannon = strstr(piece,"Cannon")!=NULL;

            if (!is_cannon) {
                for (int d=0;d<4&&count<max_moves;d++) {
                    int tr=r+dirs[d][0], tc=c+dirs[d][1];
                    if (tr<0||tr>=4||tc<0||tc>=8) continue;
                    const char* tgt=bp[tr][tc];
                    if (strcmp(tgt,"Null")==0 ||
                        (strstr(tgt,opp_color)&&ai_can_capture_by_name(piece,tgt)))
                        moves[count++] = (Move){r,c,tr,tc,false};
                }
            } else {
                // 炮移動：只能走一步到相鄰空格（不可跳躍、不可滑行）
                for (int d=0;d<4&&count<max_moves;d++) {
                    int tr=r+dirs[d][0], tc=c+dirs[d][1];
                    if (tr<0||tr>=4||tc<0||tc>=8) continue;
                    if (strcmp(bp[tr][tc],"Null")==0)
                        moves[count++] = (Move){r,c,tr,tc,false};
                }
                // 炮吃子：跳吃（中間恰好一個棋子，目標為敵方已翻棋子）
                for (int tr=0;tr<4&&count<max_moves;tr++)
                    for (int tc=0;tc<8&&count<max_moves;tc++) {
                        if (tr==r&&tc==c) continue;
                        if (!strstr(bp[tr][tc],opp_color)) continue;
                        if (cannon_can_jump_capture(bp,r,c,tr,tc,opp_color))
                            moves[count++] = (Move){r,c,tr,tc,false};
                    }
            }
        }
    }
    return count;
}

static void apply_move_struct(const char* bp[4][8], char tmp[4][8][32],
                               const char* sim[4][8], const Move* m) {
    for (int i=0;i<4;i++) for(int j=0;j<8;j++) {
        strncpy(tmp[i][j], bp[i][j], 31);
        tmp[i][j][31]='\0';
    }
    if (m->is_flip)
        strncpy(tmp[m->fr][m->fc], "Null", 31);
    else {
        strncpy(tmp[m->tr][m->tc], bp[m->fr][m->fc], 31);
        strncpy(tmp[m->fr][m->fc], "Null", 31);
    }
    for (int i=0;i<4;i++) for(int j=0;j<8;j++) sim[i][j]=tmp[i][j];
}

// ── 計算一步棋的「進攻進展分」─────────────────────────────────
// 包含：
//   a. 接近獎勵：移動後與最近敵人距離縮短
//   b. 壓制改善：移動後新增的攻擊對象
//   c. 翻牌固定獎勵
//   d. 踱步懲罰：移到空格但沒有接近也沒有改善壓制
static int move_bonus(const char* bp[4][8],
                       const Move* m,
                       const char* my_color,
                       const char* opp_color) {
    if (m->is_flip) return SCORE_FLIP_COVERED_BONUS;

    const char* tgt = bp[m->tr][m->tc];
    // 吃子不需要額外調整，吃子本身在 evaluate 已計入
    if (strstr(tgt, opp_color)) return 0;

    // pure move（到空格）
    const char* piece = bp[m->fr][m->fc];

    // a. 接近獎勵：移動前後距最近敵人的距離差
    int dist_before = 99, dist_after = 99;
    for (int er=0;er<4;er++) for(int ec=0;ec<8;ec++) {
        if (!strstr(bp[er][ec], opp_color)) continue;
        int db = manhattan(m->fr, m->fc, er, ec);
        int da = manhattan(m->tr, m->tc, er, ec);
        if (db < dist_before) dist_before = db;
        if (da < dist_after)  dist_after  = da;
    }
    int proximity_gain = dist_before - dist_after; // 正=靠近，負=遠離

    // b. 壓制改善：模擬移動後新增幾個能攻擊的敵方目標
    int dirs[4][2] = {{0,1},{0,-1},{1,0},{-1,0}};
    int attack_before=0, attack_after=0;
    bool is_cannon = strstr(piece,"Cannon")!=NULL;
    if (!is_cannon) {
        for (int d=0;d<4;d++) {
            int nr, nc;
            // 移動前
            nr=m->fr+dirs[d][0]; nc=m->fc+dirs[d][1];
            if (nr>=0&&nr<4&&nc>=0&&nc<8&&strstr(bp[nr][nc],opp_color)&&
                ai_can_capture_by_name(piece,bp[nr][nc])) attack_before++;
            // 移動後
            nr=m->tr+dirs[d][0]; nc=m->tc+dirs[d][1];
            if (nr>=0&&nr<4&&nc>=0&&nc<8&&strstr(bp[nr][nc],opp_color)&&
                ai_can_capture_by_name(piece,bp[nr][nc])) attack_after++;
        }
    }
    int attack_gain = attack_after - attack_before;

    int bonus = proximity_gain * SCORE_PROXIMITY_BONUS
              + attack_gain   * 40;

    // c. 踱步懲罰：沒有接近也沒有改善壓制
    if (proximity_gain <= 0 && attack_gain <= 0)
        bonus += SCORE_STAGNATION_PENALTY;

    return bonus;
}

// ============================================================
//  2-ply Minimax
// ============================================================

static int opponent_best_response(const char* bp[4][8],
                                   const char* my_color,
                                   const char* opp_color) {
    Move opp_moves[MAX_MOVES];
    int n = collect_moves(bp, opp_color, my_color, opp_moves, MAX_MOVES);
    if (n == 0) return evaluate_board(bp, my_color, opp_color);

    int worst = 999999;
    for (int i=0;i<n;i++) {
        char tmp2[4][8][32];
        const char* sim2[4][8];
        apply_move_struct(bp, tmp2, sim2, &opp_moves[i]);

        // 對方也加入 move_bonus（讓對方同樣避免踱步）
        int val = evaluate_board(sim2, my_color, opp_color)
                - move_bonus(bp, &opp_moves[i], opp_color, my_color);

        if (val < worst) worst = val;
    }
    return worst;
}

bool server_ai_decide(const char* json, const char* my_color,
                      char* out_action, int action_buf_size) {
    char storage[4][8][32];
    const char* bp[4][8];
    char opp_color[10];

    strcpy(opp_color, strcmp(my_color,"Red")==0 ? "Black":"Red");
    for (int i=0;i<32;i++) {
        board_get_piece_at(json, i, storage[i/8][i%8]);
        bp[i/8][i%8] = storage[i/8][i%8];
    }

    Move my_moves[MAX_MOVES];
    int n = collect_moves(bp, my_color, opp_color, my_moves, MAX_MOVES);
    if (n == 0) return false;

    int best_score = -999999;
    int best_idx   = 0;

    for (int i=0;i<n;i++) {
        char tmp[4][8][32];
        const char* sim[4][8];
        apply_move_struct(bp, tmp, sim, &my_moves[i]);

        // Minimax 分 + 這步的進攻進展分
        int score = opponent_best_response(sim, my_color, opp_color)
                  + move_bonus(bp, &my_moves[i], my_color, opp_color);

        // 立即吃子額外加分（確定得益）
        if (!my_moves[i].is_flip) {
            const char* tgt = bp[my_moves[i].tr][my_moves[i].tc];
            if (strstr(tgt, opp_color))
                score += piece_value(tgt) / 5;
        }

        if (score > best_score) {
            best_score = score;
            best_idx   = i;
        }
    }

    Move* best = &my_moves[best_idx];
    if (best->is_flip) {
        snprintf(out_action, action_buf_size, "%d %d\n", best->fr, best->fc);
        printf("[AI] FLIP (%d,%d) score=%d\n", best->fr, best->fc, best_score);
    } else {
        const char* tgt = bp[best->tr][best->tc];
        snprintf(out_action, action_buf_size, "%d %d %d %d\n",
                 best->fr, best->fc, best->tr, best->tc);
        if (strstr(tgt, opp_color))
            printf("[AI] CAPTURE %s at (%d,%d) score=%d\n",
                   tgt, best->tr, best->tc, best_score);
        else
            printf("[AI] MOVE %s (%d,%d)->(%d,%d) score=%d\n",
                   bp[best->fr][best->fc],
                   best->fr, best->fc, best->tr, best->tc, best_score);
    }

    out_action[action_buf_size-1]='\0';
    return true;
}

// ============================================================
//  本地 AI（SharedState，單機模式）
// ============================================================

static bool local_is_threatened(SharedState* gs, int r, int c) {
    Piece me = gs->board[r][c];
    if (me.isEmpty||!me.isFlipped) return false;
    int myColor=(int)me.color;
    int dirs[4][2]={{0,1},{0,-1},{1,0},{-1,0}};
    for (int d=0;d<4;d++) {
        int er=r+dirs[d][0],ec=c+dirs[d][1];
        if (er<0||er>=4||ec<0||ec>=8) continue;
        Piece e=gs->board[er][ec];
        if (e.isEmpty||!e.isFlipped||(int)e.color==myColor) continue;
        if (board_is_valid_move(gs,er,ec,r,c)) return true;
    }
    for (int ec=0;ec<8;ec++) {
        if (ec==c) continue;
        Piece e=gs->board[r][ec];
        if (e.isEmpty||!e.isFlipped||(int)e.color==myColor||e.role!=CANNON) continue;
        if (board_is_valid_move(gs,r,ec,r,c)) return true;
    }
    for (int er=0;er<4;er++) {
        if (er==r) continue;
        Piece e=gs->board[er][c];
        if (e.isEmpty||!e.isFlipped||(int)e.color==myColor||e.role!=CANNON) continue;
        if (board_is_valid_move(gs,er,c,r,c)) return true;
    }
    return false;
}

static int local_evaluate(SharedState* gs) {
    int dirs[4][2]={{0,1},{0,-1},{1,0},{-1,0}};
    int score=0;
    int cc=gs->computerColor, hc=gs->humanColor;
    if (cc<0) return 0;
    int rv[7]={1000,200,250,400,300,350,100};

    int covered=0;
    for (int r=0;r<4;r++) for(int c=0;c<8;c++)
        if (!gs->board[r][c].isEmpty&&!gs->board[r][c].isFlipped) covered++;
    bool late_game=(covered<8);

    for (int r=0;r<4;r++) for(int c=0;c<8;c++) {
        Piece p=gs->board[r][c];
        if (p.isEmpty||!p.isFlipped) continue;
        int pv=rv[(int)p.role];
        int sign=((int)p.color==cc)?1:-1;
        score+=sign*pv;
        if (local_is_threatened(gs,r,c)) score-=sign*(pv/3);
        if ((int)p.color==cc) {
            for (int d=0;d<4;d++) {
                int nr=r+dirs[d][0],nc=c+dirs[d][1];
                if (nr<0||nr>=4||nc<0||nc>=8) continue;
                Piece nb=gs->board[nr][nc];
                if (!nb.isEmpty&&nb.isFlipped&&(int)nb.color!=cc&&
                    board_is_valid_move(gs,r,c,nr,nc))
                    score+=rv[(int)nb.role]/7;
            }
            if (late_game) {
                for (int d=0;d<4;d++) {
                    int nr=r+dirs[d][0],nc=c+dirs[d][1];
                    if (nr<0||nr>=4||nc<0||nc>=8) continue;
                    if (gs->board[nr][nc].isEmpty||
                        (!gs->board[nr][nc].isEmpty&&gs->board[nr][nc].isFlipped&&
                         (int)gs->board[nr][nc].color!=cc&&
                         board_is_valid_move(gs,r,c,nr,nc)))
                        score+=SCORE_ATTACK_FIELD;
                }
            }
        }
    }

    // 進攻傾向：己方到敵方距離 vs 敵方到己方距離
    int my_total=0, opp_total=0, mc=0, oc=0;
    for (int r=0;r<4;r++) for(int c=0;c<8;c++) {
        Piece p=gs->board[r][c];
        if (p.isEmpty||!p.isFlipped) continue;
        bool is_me=((int)p.color==cc);
        int min_d=99;
        for (int er=0;er<4;er++) for(int ec=0;ec<8;ec++) {
            Piece e=gs->board[er][ec];
            if (e.isEmpty||!e.isFlipped) continue;
            if ((int)e.color==(is_me?hc:cc)) {
                int d=manhattan(r,c,er,ec);
                if (d<min_d) min_d=d;
            }
        }
        if (min_d<99) { if(is_me){my_total+=min_d;mc++;} else{opp_total+=min_d;oc++;} }
    }
    int my_avg  = mc>0  ? my_total/mc  : 10;
    int opp_avg = oc>0  ? opp_total/oc : 10;
    score += (opp_avg - my_avg) * SCORE_PROXIMITY_BONUS;

    return score;
}

typedef struct { int fr,fc,tr,tc; bool is_flip; } LocalMove;

static int local_collect_moves(SharedState* gs, int color,
                                LocalMove* moves, int max) {
    int dirs[4][2]={{0,1},{0,-1},{1,0},{-1,0}};
    int count=0;
    for (int r=0;r<4&&count<max;r++) for(int c=0;c<8&&count<max;c++) {
        Piece p=gs->board[r][c];
        if (p.isEmpty) continue;
        if (!p.isFlipped) { moves[count++]=(LocalMove){r,c,r,c,true}; continue; }
        if ((int)p.color!=color) continue;
        for (int d=0;d<4&&count<max;d++) {
            int tr=r+dirs[d][0],tc=c+dirs[d][1];
            if (tr<0||tr>=4||tc<0||tc>=8) continue;
            if (board_is_valid_move(gs,r,c,tr,tc))
                moves[count++]=(LocalMove){r,c,tr,tc,false};
        }
    }
    return count;
}

static void local_do_move(SharedState* gs,int fr,int fc,int tr,int tc){
    gs->board[tr][tc]=gs->board[fr][fc];
    gs->board[fr][fc].isEmpty=true;
}
static void local_undo_move(SharedState* gs,
                             int fr,int fc,Piece sf,
                             int tr,int tc,Piece st){
    gs->board[fr][fc]=sf; gs->board[tr][tc]=st;
}

// 本地版的移動進展分
static int local_move_bonus(SharedState* gs, const LocalMove* m,
                              int my_color, int opp_color) {
    if (m->is_flip) return SCORE_FLIP_COVERED_BONUS;
    Piece tgt = gs->board[m->tr][m->tc];
    if (!tgt.isEmpty && tgt.isFlipped && (int)tgt.color==opp_color) return 0;

    int dist_before=99, dist_after=99;
    for (int er=0;er<4;er++) for(int ec=0;ec<8;ec++) {
        Piece e=gs->board[er][ec];
        if (e.isEmpty||!e.isFlipped||(int)e.color!=opp_color) continue;
        int db=manhattan(m->fr,m->fc,er,ec);
        int da=manhattan(m->tr,m->tc,er,ec);
        if (db<dist_before) dist_before=db;
        if (da<dist_after)  dist_after=da;
    }
    int proximity_gain = dist_before - dist_after;
    if (proximity_gain<=0) return SCORE_STAGNATION_PENALTY;
    return proximity_gain * SCORE_PROXIMITY_BONUS;
}

void local_ai_move(SharedState* gs) {
    int cc=gs->computerColor, hc=gs->humanColor;
    LocalMove my_moves[MAX_MOVES];
    int n=local_collect_moves(gs,cc,my_moves,MAX_MOVES);
    if (n==0) return;

    int best_score=-999999, best_idx=0;

    for (int i=0;i<n;i++) {
        LocalMove* m=&my_moves[i];
        Piece sf=gs->board[m->fr][m->fc];
        Piece st=gs->board[m->tr][m->tc];

        if (m->is_flip) gs->board[m->fr][m->fc].isFlipped=true;
        else local_do_move(gs,m->fr,m->fc,m->tr,m->tc);

        int worst=999999;
        if (hc>=0) {
            LocalMove opp[MAX_MOVES];
            int on=local_collect_moves(gs,hc,opp,MAX_MOVES);
            for (int j=0;j<on;j++) {
                Piece osf=gs->board[opp[j].fr][opp[j].fc];
                Piece ost=gs->board[opp[j].tr][opp[j].tc];
                if (opp[j].is_flip) gs->board[opp[j].fr][opp[j].fc].isFlipped=true;
                else local_do_move(gs,opp[j].fr,opp[j].fc,opp[j].tr,opp[j].tc);

                int val=local_evaluate(gs)
                       -local_move_bonus(gs,&opp[j],hc,cc);
                if (val<worst) worst=val;

                if (opp[j].is_flip){gs->board[opp[j].fr][opp[j].fc].isFlipped=false;gs->board[opp[j].fr][opp[j].fc]=osf;}
                else local_undo_move(gs,opp[j].fr,opp[j].fc,osf,opp[j].tr,opp[j].tc,ost);
            }
            if (on==0) worst=local_evaluate(gs);
        } else {
            worst=local_evaluate(gs);
        }

        if (m->is_flip){gs->board[m->fr][m->fc].isFlipped=false;gs->board[m->fr][m->fc]=sf;}
        else local_undo_move(gs,m->fr,m->fc,sf,m->tr,m->tc,st);

        int score=worst+local_move_bonus(gs,m,cc,hc);
        if (score>best_score){best_score=score;best_idx=i;}
    }

    LocalMove* best=&my_moves[best_idx];
    if (best->is_flip){
        gs->board[best->fr][best->fc].isFlipped=true;
        if (gs->computerColor==-1){
            gs->computerColor=(int)gs->board[best->fr][best->fc].color;
            gs->humanColor=1-gs->computerColor;
        }
        printf("[LocalAI] FLIP (%d,%d) score=%d\n",best->fr,best->fc,best_score);
    } else {
        printf("[LocalAI] MOVE (%d,%d)->(%d,%d) score=%d\n",
               best->fr,best->fc,best->tr,best->tc,best_score);
        local_do_move(gs,best->fr,best->fc,best->tr,best->tc);
    }
}
#ifndef BOARD_H
#define BOARD_H

#include "game_state.h"
#include <stdbool.h>

// ============================================================
//  board.h — 棋盤邏輯（初始化、合法移動、勝負判定、JSON解析）
//  不依賴 Raylib 或網路，純粹邏輯層
// ============================================================

// 棋子名稱對照表（繪圖時用）
extern const char* RED_NAMES[7];
extern const char* BLACK_NAMES[7];

// ---------- 初始化 ----------

// 洗牌並初始化棋盤（本地模式用）
void board_init_and_shuffle(SharedState* gs);

// 清空棋盤（全部設 isEmpty）
void board_clear(SharedState* gs);

// ---------- 規則判斷 ----------

// 取得棋子的階級 (King=7 ... Soldier=1, 未翻/空=0)
int board_get_rank_by_name(const char* piece_name);

// 棋子階級 (直接從 Piece struct)
int board_get_rank(PieceRole role);

// 判斷從 (sr,sc) 移動/吃到 (dr,dc) 是否合法
// 注意：翻牌動作不用此函數
bool board_is_valid_move(SharedState* gs, int sr, int sc, int dr, int dc);

// 判斷 (r,c) 的棋子是否受到敵方威脅（供 AI 用）
bool board_is_under_threat(SharedState* gs, int r, int c);

// ---------- 勝負判定 ----------

// 檢查勝負，若分出勝負則更新 gs->state
void board_check_win(SharedState* gs);

// ---------- JSON 解析（伺服器更新用）----------

// 從 JSON 字串解析 board 陣列，更新 gs->board
// 回傳 true 表示解析成功
bool board_parse_json(SharedState* gs, const char* json);

// 從 JSON 取得指定位置的棋子名稱（原始字串，如 "Red_King"）
void board_get_piece_at(const char* json, int index, char* out_piece);

// 從 JSON 取得指定角色(A/B)的顏色
void board_get_role_color(const char* json, const char* role, char* out_color);

// 從 JSON 更新當前回合角色
// 回傳當前 current_turn_role 的字元 ('A'/'B'), 若無則回傳 '\0'
char board_get_current_turn_role(const char* json);

// 從 JSON 取得 total_moves
int board_get_total_moves(const char* json);

// 從 JSON 取得 state 字串（"waiting" / "playing" / "finished"）
void board_get_game_state_str(const char* json, char* out_state);

// 將 JSON 棋盤字串轉成本地 Piece 結構
// piece_name 格式: "Red_King", "Black_Soldier", "Covered", "Null"
void board_name_to_piece(const char* piece_name, Piece* out);

#endif // BOARD_H
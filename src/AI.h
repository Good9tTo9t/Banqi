#ifndef AI_H
#define AI_H

#include "game_state.h"

// ============================================================
//  ai.h — AI 邏輯模組
//
//  提供兩種 AI：
//    1. local_ai_move()  — 本地單機模式，直接操作 gs->board
//    2. server_ai_move() — 伺服器連線模式，產生指令字串後送出
// ============================================================

// 本地模式 AI：執行一步（直接修改棋盤）
// computerColor: 0=紅, 1=黑
void local_ai_move(SharedState* gs);

// 伺服器模式 AI：根據 JSON 產生最佳指令，存入 out_action
// out_action 格式: "r c tr tc\n"（移動/吃）或 "r c\n"（翻牌）
// 回傳 true 表示成功產生指令
bool server_ai_decide(const char* json, const char* my_color, char* out_action, int action_buf_size);

// 工具：判斷攻擊者能否吃掉目標（伺服器棋子名稱字串版本）
// attacker / victim: "Red_King", "Black_Soldier", "Null", "Covered" 等
bool ai_can_capture_by_name(const char* attacker, const char* victim);

#endif // AI_H
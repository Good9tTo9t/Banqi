#ifndef NETWORK_H
#define NETWORK_H

#include "game_state.h"
#include <stdbool.h>

// ============================================================
//  network.h — 網路層
//
//  設計重點：
//    - 使用 Windows CreateThread 在背景執行緒跑 recv()
//    - 主執行緒（Raylib 遊戲迴圈）透過 network_poll() 取得最新更新
//    - 用 CRITICAL_SECTION 保護共享緩衝區，避免 race condition
//    - send 操作在主執行緒直接執行（send 本身不阻塞）
// ============================================================

// ---------- 生命週期 ----------

// 初始化 WinSock 並連線到伺服器
// 回傳 0 表示成功，-1 失敗
int  network_init(void);

// 加入房間：傳送 "JOIN <room_id>\n" 並等待 SUCCESS 回應
// room_id: 傳入的房間號碼字串
// 回傳 0 成功，-1 失敗
// 成功後會設定 out_role（"first" 或 "second"）
int  network_join_room(const char* room_id, char* out_role, int role_buf_size);

// 啟動背景接收執行緒（join 成功後呼叫）
void network_start_recv_thread(void);

// 關閉連線與清理
void network_close(void);

// ---------- 資料存取（主執行緒呼叫）----------

// 傳送動作字串（格式: "r c tr tc\n" 或 "r c\n"）
void network_send_action(const char* action);

// 輪詢：如果背景執行緒有新的 UPDATE 訊息，複製到 out_buf 並回傳 true
// 若沒有新訊息則回傳 false
// 這個函數在主執行緒（Raylib 迴圈）每幀呼叫
bool network_poll(char* out_buf, int buf_size);

// ---------- 狀態查詢 ----------

// 是否已成功連線
bool network_is_connected(void);

// 取得分配的角色字串（"first" / "second"）
const char* network_get_role(void);

#endif // NETWORK_H
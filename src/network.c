#include "network.h"
#include "dark_chess_client.h"  // 保留底層 socket 定義
#include <stdio.h>
#include <string.h>
#include <windows.h>

// ============================================================
//  network.c — 網路層實作
//
//  架構說明：
//    _recv_thread       : 背景執行緒，持續阻塞在 recv()
//    _pending_buf       : 背景執行緒寫入，主執行緒讀取
//    _pending_lock      : CRITICAL_SECTION 保護 _pending_buf
//    _has_pending       : 旗標，通知主執行緒有新資料
// ============================================================

#define RECV_BUF_SIZE 8192

static bool            _connected      = false;
static char            _role[16]       = "";

// 背景執行緒共享區
static HANDLE          _recv_thread    = NULL;
static CRITICAL_SECTION _pending_lock;
static char            _pending_buf[RECV_BUF_SIZE] = {0};
static volatile bool   _has_pending    = false;
static volatile bool   _thread_running = false;

// ── 背景接收執行緒 ───────────────────────────────────────────

static DWORD WINAPI recv_thread_func(LPVOID param) {
    (void)param;
    char tmp[RECV_BUF_SIZE];
    _thread_running = true;

    while (_thread_running && _global_socket != INVALID_SOCKET) {
        memset(tmp, 0, sizeof(tmp));
        int size = recv(_global_socket, tmp, RECV_BUF_SIZE - 1, 0);

        if (size <= 0) {
            // 連線中斷
            printf("[Network] Connection lost.\n");
            _connected = false;
            break;
        }
        tmp[size] = '\0';

        // 只關心 UPDATE 訊息
        if (strstr(tmp, "UPDATE")) {
            EnterCriticalSection(&_pending_lock);
            strncpy(_pending_buf, tmp, RECV_BUF_SIZE - 1);
            _pending_buf[RECV_BUF_SIZE - 1] = '\0';
            _has_pending = true;
            LeaveCriticalSection(&_pending_lock);
        } else {
            // 其他訊息（如 WIN/LOSE/DRAW）直接印出到 console
            printf("[Server] %s\n", tmp);
        }
    }

    _thread_running = false;
    return 0;
}

// ── 生命週期 ─────────────────────────────────────────────────

int network_init(void) {
    InitializeCriticalSection(&_pending_lock);

    if (init_connection() != 0) {
        printf("[Network] Failed to connect to server.\n");
        return -1;
    }

    _connected = true;
    printf("[Network] Connected!\n");
    return 0;
}

int network_join_room(const char* room_id, char* out_role, int role_buf_size) {
    if (!_connected) return -1;

    char send_buf[128];
    char response[2048];

    // 最多重試 3 次
    for (int attempt = 0; attempt < 3; attempt++) {
        snprintf(send_buf, sizeof(send_buf), "JOIN %s\n", room_id);
        send(_global_socket, send_buf, (int)strlen(send_buf), 0);
        printf("[Network] Sent: %s", send_buf);

        int size = recv(_global_socket, response, sizeof(response) - 1, 0);
        if (size <= 0) return -1;
        response[size] = '\0';
        printf("[Server] %s\n", response);

        if (strstr(response, "SUCCESS")) {
            // 解析 ROLE
            char* role_ptr = strstr(response, "ROLE ");
            if (role_ptr) {
                sscanf(role_ptr + 5, "%15s", _role);
                if (out_role)
                    strncpy(out_role, _role, role_buf_size - 1);
                printf("[Network] Assigned role: %s\n", _role);
            }
            return 0;
        }

        printf("[Network] Join attempt %d failed, retrying...\n", attempt + 1);
        Sleep(500);
    }
    return -1;
}

void network_start_recv_thread(void) {
    if (_recv_thread) return;
    _recv_thread = CreateThread(NULL, 0, recv_thread_func, NULL, 0, NULL);
    if (!_recv_thread) {
        printf("[Network] Failed to create recv thread!\n");
    } else {
        printf("[Network] Receive thread started.\n");
    }
}

void network_close(void) {
    _thread_running = false;
    close_connection();
    if (_recv_thread) {
        WaitForSingleObject(_recv_thread, 2000);
        CloseHandle(_recv_thread);
        _recv_thread = NULL;
    }
    DeleteCriticalSection(&_pending_lock);
    _connected = false;
}

// ── 資料存取（主執行緒）──────────────────────────────────────

void network_send_action(const char* action) {
    if (!_connected || _global_socket == INVALID_SOCKET) return;
    send_action(action);
    printf("[Network] Sent action: %s", action);
}

bool network_poll(char* out_buf, int buf_size) {
    if (!_has_pending) return false;

    EnterCriticalSection(&_pending_lock);
    strncpy(out_buf, _pending_buf, buf_size - 1);
    out_buf[buf_size - 1] = '\0';
    _has_pending = false;
    LeaveCriticalSection(&_pending_lock);

    return true;
}

// ── 狀態查詢 ─────────────────────────────────────────────────

bool network_is_connected(void) {
    return _connected;
}

const char* network_get_role(void) {
    return _role;
}
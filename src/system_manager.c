#include "system_manager.h"
#include "status_manager.h"
#include "watchdog_manager.h"
#include "uart_bridge.h"
#include "bt_spp_handler.h"
#include "data_transfer_engine.h"
#include "config_manager.h"
#include "flash_storage.h"
#include "switch_reader.h"
#include "pin_config.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static system_state_t g_system_state = {0};
static system_config_t g_system_config = {
    .uart_baudrate = 115200,
    .uart_data_bits = 8,
    .uart_stop_bits = 1,
    .uart_parity = 0,  // UART_PARITY_NONE（起動時にSW状態で上書き）
    .watchdog_timeout_ms = WDT_TIMEOUT_MS,
    .device_name = "BT Serial Bridge",
    .pin_code = "0000",
    .debug_logging_enabled = true
};

static bool system_initialized = false;

system_init_result_t system_manager_init(void) {
    if (system_initialized) {
        status_manager_log(LOG_LEVEL_WARN, "System already initialized");
        return SYSTEM_INIT_SUCCESS;
    }
    
    printf("Starting system initialization...\n");
    
    // 1. CYW43アーキテクチャ初期化（最初に実行）
    if (cyw43_arch_init()) {
        printf("Failed to initialize CYW43 architecture\n");
        return SYSTEM_INIT_ERROR_CYW43;
    }
    printf("CYW43 architecture initialized\n");
    
    // 2. Status Manager初期化（CYW43初期化後）
    if (status_manager_init() != 0) {
        printf("Failed to initialize Status Manager\n");
        return SYSTEM_INIT_ERROR_STATUS_MANAGER;
    }
    status_manager_log(LOG_LEVEL_INFO, "Status Manager initialized");
    
    // デバッグログレベル設定
    if (g_system_config.debug_logging_enabled) {
        status_manager_set_log_level(LOG_LEVEL_DEBUG);
    } else {
        status_manager_set_log_level(LOG_LEVEL_INFO);
    }
    
    // 3. SW状態に応じたUART設定を適用
    uint8_t sw_state = switch_reader_get_state();

    if ((sw_state & 0x03) == 0x03) {
        // SW=11: 不揮発ROMからUART設定のみ読み込み
        config_load_result_t load_result = config_manager_load_uart(&g_system_config);
        if (load_result == CONFIG_LOAD_SUCCESS) {
            const char *parity_str = (g_system_config.uart_parity == UART_PARITY_ODD) ? "O" :
                                     (g_system_config.uart_parity == UART_PARITY_EVEN) ? "E" : "N";
            status_manager_log(LOG_LEVEL_INFO,
                "SW=11: Loaded from flash: %lu-%d-%s-%d",
                g_system_config.uart_baudrate,
                g_system_config.uart_data_bits,
                parity_str,
                g_system_config.uart_stop_bits);
        } else {
            // フラッシュに有効な設定がない場合はデフォルトにフォールバック
            status_manager_log(LOG_LEVEL_WARN,
                "SW=11: Flash config not found (err=%d), using defaults", load_result);
            config_manager_reset_to_defaults(&g_system_config);
        }
    } else {
        // SW=00/01/10: プリセット適用
        config_manager_apply_sw_uart_preset(&g_system_config, sw_state);
    }
    
    // 4. UART Bridge初期化（CYW43初期化後、競合を避けるため）
    uart_config_t uart_config = {
        .baudrate = g_system_config.uart_baudrate,
        .data_bits = g_system_config.uart_data_bits,
        .stop_bits = g_system_config.uart_stop_bits,
        .parity = g_system_config.uart_parity
    };
    
    if (uart_bridge_init(&uart_config) != 0) {
        status_manager_log(LOG_LEVEL_ERROR, "Failed to initialize UART Bridge");
        return SYSTEM_INIT_ERROR_UART;
    }
    status_manager_log(LOG_LEVEL_INFO, "UART Bridge initialized");
    g_system_state.uart_initialized = true;
    
    // 5. Watchdog Manager初期化
    watchdog_config_t watchdog_config = {
        .timeout_ms = g_system_config.watchdog_timeout_ms,
        .reset_count_threshold = WDT_RESET_THRESHOLD
    };
    
    if (watchdog_manager_init(&watchdog_config) != 0) {
        status_manager_log(LOG_LEVEL_ERROR, "Failed to initialize Watchdog Manager");
        return SYSTEM_INIT_ERROR_WATCHDOG;
    }
    status_manager_log(LOG_LEVEL_INFO, "Watchdog Manager initialized");
    
    // 6. Data Transfer Engine初期化
    if (data_transfer_init() != 0) {
        status_manager_log(LOG_LEVEL_ERROR, "Failed to initialize Data Transfer Engine");
        return SYSTEM_INIT_ERROR_DATA_TRANSFER;
    }
    status_manager_log(LOG_LEVEL_INFO, "Data Transfer Engine initialized");
    
    // 7. Bluetooth SPP Handler初期化
    if (bt_spp_init() != 0) {
        status_manager_log(LOG_LEVEL_ERROR, "Failed to initialize Bluetooth SPP Handler");
        return SYSTEM_INIT_ERROR_BLUETOOTH;
    }
    status_manager_log(LOG_LEVEL_INFO, "Bluetooth SPP Handler initialized");
    
    // システム状態初期化
    g_system_state.bt_connected = false;
    g_system_state.uptime_seconds = 0;
    g_system_state.watchdog_reset_count = watchdog_manager_get_reset_count();
    
    system_initialized = true;
    status_manager_log(LOG_LEVEL_INFO, "System initialization completed successfully");
    
    return SYSTEM_INIT_SUCCESS;
}

void system_manager_process(void) {
    if (!system_initialized) {
        return;
    }
    
    static uint32_t last_uptime_update = 0;
    static uint32_t last_slow_process = 0;
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // 1秒ごとにアップタイムを更新
    if (current_time - last_uptime_update >= 1000) {
        g_system_state.uptime_seconds++;
        last_uptime_update = current_time;
    }
    
    // 高頻度処理
    data_transfer_process();
    
    // 低頻度処理（100ms毎）
    if (current_time - last_slow_process >= 100) {
        status_manager_process();
        bt_spp_process_auto_reconnect();
        
        // Bluetooth接続状態を更新
        bt_connection_state_t bt_state = bt_spp_get_connection_state();
        g_system_state.bt_connected = (bt_state == BT_STATE_CONNECTED);
        
        last_slow_process = current_time;
    }
    
    // ウォッチドッグをフィード
    watchdog_manager_feed();
}

system_state_t* system_manager_get_state(void) {
    return &g_system_state;
}

system_config_t* system_manager_get_config(void) {
    return &g_system_config;
}


void system_manager_print_status(void) {
    status_manager_log(LOG_LEVEL_INFO, "=== System Status ===");
    status_manager_log(LOG_LEVEL_INFO, "Initialized: %s", system_initialized ? "Yes" : "No");
    status_manager_log(LOG_LEVEL_INFO, "Uptime: %d seconds", g_system_state.uptime_seconds);
    status_manager_log(LOG_LEVEL_INFO, "Bluetooth connected: %s", g_system_state.bt_connected ? "Yes" : "No");
    status_manager_log(LOG_LEVEL_INFO, "UART initialized: %s", g_system_state.uart_initialized ? "Yes" : "No");
    status_manager_log(LOG_LEVEL_INFO, "Watchdog reset count: %d", g_system_state.watchdog_reset_count);
    status_manager_log(LOG_LEVEL_INFO, "====================");
    
    // 各モジュールの詳細情報
    bt_spp_get_connection_info();
    data_transfer_print_stats();
    uart_bridge_print_error_stats();  // 追加
    watchdog_manager_print_diagnostics();
}

void system_manager_print_uart_config(void) {
    printf("Current UART Configuration:\n");
    printf("  Baudrate:   %lu bps\n", g_system_config.uart_baudrate);
    printf("  Data bits:  %d\n", g_system_config.uart_data_bits);
    printf("  Stop bits:  %d\n", g_system_config.uart_stop_bits);
    
    const char* parity_str;
    switch (g_system_config.uart_parity) {
        case 0: parity_str = "None"; break;
        case 1: parity_str = "Even"; break;  // 実際のハードウェア定義に合わせて修正
        case 2: parity_str = "Odd"; break;   // 実際のハードウェア定義に合わせて修正
        default: parity_str = "Unknown"; break;
    }
    printf("  Parity:     %s\n", parity_str);
    printf("  Format:     %lu-%d-%s-%d\n", 
           g_system_config.uart_baudrate,
           g_system_config.uart_data_bits,
           parity_str,
           g_system_config.uart_stop_bits);
}

// --- ブロッキング対話式UART設定 ---

// ウォッチドッグフィード付き1文字読み取り（100msタイムアウトでリトライ）
static int console_getchar(void) {
    while (true) {
        int ch = getchar_timeout_us(100000);  // 100msタイムアウト
        if (ch != PICO_ERROR_TIMEOUT) {
            return ch;
        }
        watchdog_manager_feed();
    }
}

// 数値入力（Enter確定、ESC/空Enterでキャンセル）戻り値: 数値 or -1
static int32_t console_read_number(void) {
    char buf[12];
    uint8_t pos = 0;

    while (true) {
        int ch = console_getchar();

        if (ch == '\r' || ch == '\n') {
            putchar('\n');
            if (pos == 0) return -1;
            buf[pos] = '\0';
            return (int32_t)strtoul(buf, NULL, 10);
        }
        if (ch == 0x1B) {  // ESC
            printf(" (cancelled)\n");
            return -1;
        }
        if (ch == '\b' || ch == 0x7F) {  // BS / DEL
            if (pos > 0) {
                pos--;
                printf("\b \b");
            }
            continue;
        }
        if (ch >= '0' && ch <= '9' && pos < sizeof(buf) - 1) {
            buf[pos++] = (char)ch;
            putchar(ch);
        }
    }
}

// 1文字入力（ESCでキャンセル）戻り値: 文字 or -1
static int console_read_char(void) {
    int ch = console_getchar();
    if (ch == 0x1B) {
        printf(" (cancelled)\n");
        return -1;
    }
    return ch;
}

// UART設定をハードウェアに即時反映
static void apply_uart_config(void) {
    uart_config_t cfg = {
        .baudrate  = g_system_config.uart_baudrate,
        .data_bits = g_system_config.uart_data_bits,
        .stop_bits = g_system_config.uart_stop_bits,
        .parity    = g_system_config.uart_parity
    };
    uart_bridge_set_config(&cfg);
}

static const char* parity_to_str(uint8_t parity) {
    switch (parity) {
        case 0: return "None";
        case 1: return "Even";
        case 2: return "Odd";
        default: return "?";
    }
}

static void print_uart_summary(void) {
    printf("  Current: %lu-%d-%s-%d\n",
           g_system_config.uart_baudrate,
           g_system_config.uart_data_bits,
           parity_to_str(g_system_config.uart_parity),
           g_system_config.uart_stop_bits);
}

void system_manager_uart_config_interactive(void) {
    printf("\n=== UART Configuration ===\n");
    print_uart_summary();
    printf("  B - Baudrate (1200-921600)\n");
    printf("  D - Data bits (5-8)\n");
    printf("  P - Parity (N/E/O)\n");
    printf("  T - Stop bits (1-2)\n");
    printf("  W - Save to flash (for SW=11)\n");
    printf("  Q - Quit\n");
    printf("==========================\n");

    bool done = false;
    while (!done) {
        printf("UART> ");
        int ch = console_getchar();
        putchar(ch);
        putchar('\n');

        switch (ch) {
            case 'b': case 'B': {
                printf("Baudrate (%lu)> ", g_system_config.uart_baudrate);
                int32_t val = console_read_number();
                if (val < 0) break;
                if (val < 1200 || val > 921600) {
                    printf("  Invalid (1200-921600)\n");
                    break;
                }
                uint32_t old = g_system_config.uart_baudrate;
                g_system_config.uart_baudrate = (uint32_t)val;
                apply_uart_config();
                printf("  Baudrate: %lu -> %lu\n", old, g_system_config.uart_baudrate);
                break;
            }
            case 'd': case 'D': {
                printf("Data bits (%d)> ", g_system_config.uart_data_bits);
                int c = console_read_char();
                if (c < 0) break;
                putchar(c);
                putchar('\n');
                if (c < '5' || c > '8') {
                    printf("  Invalid (5-8)\n");
                    break;
                }
                uint8_t old = g_system_config.uart_data_bits;
                g_system_config.uart_data_bits = (uint8_t)(c - '0');
                apply_uart_config();
                printf("  Data bits: %d -> %d\n", old, g_system_config.uart_data_bits);
                break;
            }
            case 'p': case 'P': {
                printf("Parity (%s) [N/E/O]> ", parity_to_str(g_system_config.uart_parity));
                int c = console_read_char();
                if (c < 0) break;
                putchar(c);
                putchar('\n');
                uint8_t new_parity;
                switch (c) {
                    case 'n': case 'N': new_parity = UART_PARITY_NONE; break;
                    case 'e': case 'E': new_parity = UART_PARITY_EVEN; break;
                    case 'o': case 'O': new_parity = UART_PARITY_ODD;  break;
                    default:
                        printf("  Invalid (N/E/O)\n");
                        continue;
                }
                printf("  Parity: %s -> %s\n",
                       parity_to_str(g_system_config.uart_parity),
                       parity_to_str(new_parity));
                g_system_config.uart_parity = new_parity;
                apply_uart_config();
                break;
            }
            case 't': case 'T': {
                printf("Stop bits (%d)> ", g_system_config.uart_stop_bits);
                int c = console_read_char();
                if (c < 0) break;
                putchar(c);
                putchar('\n');
                if (c != '1' && c != '2') {
                    printf("  Invalid (1-2)\n");
                    break;
                }
                uint8_t old = g_system_config.uart_stop_bits;
                g_system_config.uart_stop_bits = (uint8_t)(c - '0');
                apply_uart_config();
                printf("  Stop bits: %d -> %d\n", old, g_system_config.uart_stop_bits);
                break;
            }
            case 'w': case 'W': {
                printf("Saving UART config to flash...\n");
                config_save_result_t result = config_manager_save_uart(&g_system_config);
                if (result == CONFIG_SAVE_SUCCESS) {
                    printf("  Saved. Will be used when SW=11.\n");
                } else {
                    printf("  Save failed (err=%d)\n", result);
                }
                break;
            }
            case 'c': case 'C':
                print_uart_summary();
                break;
            case 'q': case 'Q':
                done = true;
                break;
            default:
                printf("  B/D/P/T/W/C/Q\n");
                break;
        }
    }

    printf("=== Done ===\n");
    print_uart_summary();
    printf("\n");
}

// システムシャットダウン（テスト用）
void system_manager_shutdown(void) {
    if (!system_initialized) {
        return;
    }
    
    status_manager_log(LOG_LEVEL_INFO, "System shutdown initiated");
    
    // Bluetooth切断
    bt_spp_disconnect();
    
    // ウォッチドッグ無効化
    watchdog_manager_disable();
    
    system_initialized = false;
    status_manager_log(LOG_LEVEL_INFO, "System shutdown completed");
}
/*
 * Bluetooth Serial Bridge - Main Application
 * 
 * This application creates a transparent bridge between Bluetooth SPP and UART,
 * allowing PC applications to communicate with target boards wirelessly.
 */

#include "pico/stdlib.h"
#include "btstack_run_loop.h"
#include "system_manager.h"
#include "status_manager.h"
#include "data_transfer_engine.h"
#include <stdio.h>

#include "debug_gpio.h"
#include "switch_reader.h"
#include "version.h"



int btstack_main(int argc, const char * argv[]);

int main() {
    stdio_init_all();
    debug_gpio_init();
    switch_reader_init();

    sleep_ms(5000); //安定待ち

    printf("\n=== Bluetooth Serial Bridge Starting ===\n");
    printf("Version: %s\n", FW_VERSION_STRING);
    printf("Build: %s %s\n", __DATE__, __TIME__);
    printf("========================================\n\n");
    
    // システム初期化
    system_init_result_t init_result = system_manager_init();
    if (init_result != SYSTEM_INIT_SUCCESS) {
        printf("System initialization failed with error: %d\n", init_result);
        return -1;
    }
    
    printf("System initialized successfully. Starting main loop...\n");
    printf("\n=== Debug Commands ===\n");
    printf("Press 's' to show statistics\n");
    printf("Press 'r' to reset statistics\n");
    printf("Press 'c' to show current UART configuration\n");
    printf("Press 'u' to change UART configuration\n");
    printf("Press 'h' for help\n");
    printf("=====================\n\n");
    
    // BTStackメインループを開始
    btstack_main(0, NULL);
    btstack_run_loop_execute();
    
    return 0;
}

// システム処理タイマーのハンドラ
static void system_timer_handler(struct btstack_timer_source *ts) {
        
    debug_gpio_set(PIN_DEBUG_0,1);

    // USBCDCからのキー入力をチェック
    int ch = getchar_timeout_us(0);  // ノンブロッキングでキー入力をチェック
    if (ch != PICO_ERROR_TIMEOUT) {
        switch (ch) {
            case 's':
            case 'S':
                // 統計情報を出力
                printf("\n=== Statistics Output Triggered by User ===\n");
                system_manager_print_status();
                printf("=== End of Statistics ===\n\n");
                break;
            case 'r':
            case 'R':
                // 統計をリセット
                printf("\n=== Statistics Reset Triggered by User ===\n");
                data_transfer_reset_stats();
                printf("Statistics have been reset.\n\n");
                break;
            case 'c':
            case 'C':
                // UART設定を表示
                printf("\n=== Current UART Configuration ===\n");
                system_manager_print_uart_config();
                printf("=== End of UART Configuration ===\n\n");
                break;
            case 'u':
            case 'U':
                // UART設定変更（ブロッキング対話モード）
                system_manager_uart_config_interactive();
                break;
            case 'h':
            case 'H':
            case '?':
                // ヘルプを表示
                printf("\n=== Available Commands ===\n");
                printf("s/S - Show statistics\n");
                printf("r/R - Reset statistics\n");
                printf("c/C - Show current UART configuration\n");
                printf("u/U - Change UART configuration\n");
                printf("h/H/? - Show this help\n");
                printf("========================\n\n");
                break;
            default:
                // 無効なキーの場合は何もしない
                break;
        }
    }

    // システムの定期処理を実行
    system_manager_process();
    
    // 次のタイマーを設定
    btstack_run_loop_set_timer(ts, 1);
    btstack_run_loop_add_timer(ts);

    debug_gpio_set(PIN_DEBUG_0,0);
}

// BTStackメイン関数（既存のSPP実装を統合）
int btstack_main(int argc, const char * argv[]) {
    (void)argc;
    (void)argv;
    
    // システムの定期処理を開始
    static btstack_timer_source_t system_timer;

    // システム処理タイマーを設定
    system_timer.process = &system_timer_handler;
    btstack_run_loop_set_timer(&system_timer, 1);
    btstack_run_loop_add_timer(&system_timer);
    
    status_manager_log(LOG_LEVEL_INFO, "Bluetooth Serial Bridge ready for connections");
    
    return 0;
}

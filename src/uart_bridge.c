/*
 * UART Bridge - DMA Mode
 * 
 * BT→UART送信はDMA転送を使用
 * UART→BT受信は従来の割り込み処理を維持
 * 送信完了割り込み関連処理は削除済み
 */

#include "uart_bridge.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include <stdio.h>
#include "debug_gpio.h"

// UART定義（pin_config.hのUART_PERIPHを使用）
#define UART_ID UART_PERIPH

// エラー処理の設定
#define MAX_CONSECUTIVE_ERRORS 100
#define ERROR_RECOVERY_DELAY_MS 10

static uart_data_received_callback_t g_data_callback = NULL;
static uart_error_callback_t g_error_callback = NULL;
// 送信完了コールバックは削除（DMAを使用するため）
static uart_error_stats_t error_stats = {0};

// 送信関連デバッグカウンターは削除（DMAを使用するため）

// エラー処理関数
static void handle_uart_errors(uint32_t error_status) {
    uart_error_type_t error_type = UART_ERROR_NONE;
    
    // オーバーランエラー
    if (error_status & UART_UARTMIS_OEMIS_BITS) {
        error_stats.overrun_errors++;
        error_type |= UART_ERROR_OVERRUN;
        
        // オーバーランエラーをクリア
        uart_get_hw(UART_ID)->icr = UART_UARTICR_OEIC_BITS;
    }
    
    // ブレークエラー
    if (error_status & UART_UARTMIS_BEMIS_BITS) {
        error_stats.break_errors++;
        error_type |= UART_ERROR_BREAK;
        
        // ブレークエラーをクリア
        uart_get_hw(UART_ID)->icr = UART_UARTICR_BEIC_BITS;
    }
    
    // パリティエラー
    if (error_status & UART_UARTMIS_PEMIS_BITS) {
        error_stats.parity_errors++;
        error_type |= UART_ERROR_PARITY;
        
        // パリティエラーをクリア
        uart_get_hw(UART_ID)->icr = UART_UARTICR_PEIC_BITS;
    }
    
    // フレーミングエラー
    if (error_status & UART_UARTMIS_FEMIS_BITS) {
        error_stats.framing_errors++;
        error_type |= UART_ERROR_FRAMING;
        
        // フレーミングエラーをクリア
        uart_get_hw(UART_ID)->icr = UART_UARTICR_FEIC_BITS;
    }
    
    if (error_type != UART_ERROR_NONE) {
        error_stats.total_errors++;
        error_stats.consecutive_errors++;
        
        if (error_stats.consecutive_errors > error_stats.max_consecutive_errors) {
            error_stats.max_consecutive_errors = error_stats.consecutive_errors;
        }
        
        // エラーコールバック呼び出し
        if (g_error_callback) {
            g_error_callback(error_type, error_stats.total_errors);
        }
        
        // 連続エラーが多すぎる場合の緊急処理
        if (error_stats.consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
            // UART受信を一時的に無効化
            uart_set_irq_enables(UART_ID, false, uart_is_enabled(UART_ID));
            
            // 短時間待機後に再有効化（別の処理で実行される想定）
            // printf("UART: Too many consecutive errors, temporarily disabling RX\n");  // 割り込み内ログ無効化
        }
    }
}


// UART割り込みハンドラ（リングバッファ版）
static void uart_irq_handler() {
    uint32_t status = uart_get_hw(UART_ID)->mis;  // 割り込み状態を取得
    uint32_t loop_count = 0;
    const uint32_t MAX_LOOP_COUNT = 1000;  // デッドロック防止
    debug_gpio_set(PIN_DEBUG_1,0);
    
    // エラー割り込み処理（最優先）
    if (status & (UART_UARTMIS_OEMIS_BITS | UART_UARTMIS_BEMIS_BITS | 
                  UART_UARTMIS_PEMIS_BITS | UART_UARTMIS_FEMIS_BITS)) {
        handle_uart_errors(status);
    }
    
    // 受信タイムアウト処理
    if (status & UART_UARTMIS_RTMIS_BITS) {
        // 特殊動作なし
    }
    
    // 受信処理（デッドロック防止付き）- 直接コールバック呼び出し
    while (uart_is_readable(UART_ID) && loop_count < MAX_LOOP_COUNT) {
        uint32_t dr_value = uart_get_hw(UART_ID)->dr;
        uint8_t errors = (dr_value >> 8) & 0x0F; // エラーフラグ
        
        // 受信データ総バイト数をカウント（エラーがあっても）
        error_stats.total_bytes_received++;
        
        if (errors == 0) {
            uint8_t ch = dr_value & 0xFF;
            // エラーなく受信したバイト数をカウント
            error_stats.valid_bytes_received++;
            
            // 直接コールバックに渡す（バッファリングなし）
            if (g_data_callback) {
                g_data_callback(&ch, 1);
            }
        }

        loop_count++;
        
        // 正常データ受信時は連続エラーカウンタをリセット
        error_stats.consecutive_errors = 0;
    }
    
    // デッドロック検出
    if (loop_count >= MAX_LOOP_COUNT) {
        error_stats.total_errors++;
        if (g_error_callback) {
            g_error_callback(UART_ERROR_OVERRUN, error_stats.total_errors);
        }
    }
    
    // 送信完了割り込み処理は削除（DMAを使用するため）
    
    debug_gpio_set(PIN_DEBUG_1,1);
}

int uart_bridge_init(uart_config_t *config) {
    if (!config) {
        return -1;
    }
    
    printf("Initializing UART Bridge on GPIO %d (TX), %d (RX)\n", PIN_UART_TX, PIN_UART_RX);
    
    // UART初期化
    uart_init(UART_ID, config->baudrate);
    
    // GPIO設定を先に行う
    gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_UART_RX, GPIO_FUNC_UART);
    
    // UART設定
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, config->data_bits, config->stop_bits, (uart_parity_t)config->parity);
    uart_set_fifo_enabled(UART_ID, true);
    
    uart_hw_t *uart_hw = uart_get_hw(UART_ID);
    
    // 受信タイムアウト = 32 bit periods (ハードウェア固定)
    uint32_t actual_timeout_us = (32 * 1000000) / config->baudrate;
    printf("UART Hardware timeout: %lu μs (32 bit periods @ %lu bps)\n",
           actual_timeout_us, config->baudrate);
    
    // IFLS (Interrupt FIFO Level Select) レジスタで受信・送信閾値設定
    // 小さなデータパケットに対応するため、最小閾値に設定
    uart_hw->ifls = (uart_hw->ifls & ~UART_UARTIFLS_RXIFLSEL_BITS) | 
                    (0 << UART_UARTIFLS_RXIFLSEL_LSB) |  // 受信: 1/8 full (4 bytes)
                    (uart_hw->ifls & ~UART_UARTIFLS_TXIFLSEL_BITS) |
                    (0 << UART_UARTIFLS_TXIFLSEL_LSB);   // 送信: 1/8 full (2 bytes)
    
    // 代替案: さらに応答性を高める場合
    // FIFOを無効化して1バイトごとに割り込み発生させることも可能
    // uart_set_fifo_enabled(UART_ID, false);  // 1文字ごとに割り込み
    
    // 現在の設定での動作:
    // - 4バイト以上: 即座に割り込み
    // - 1-3バイト: タイムアウト後に割り込み（約0.28ms@115200bps）
    
    // バッファ初期化は不要（リングバッファはdata_transfer_engineで管理）
    
    
    // 割り込み設定（受信、エラー、タイムアウト有効、送信は必要時に有効化）
    int uart_irq = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;
    
    // 既存の割り込みハンドラがあれば無効化
    irq_set_enabled(uart_irq, false);
    
    // 新しいハンドラを設定
    irq_set_exclusive_handler(uart_irq, uart_irq_handler);
    irq_set_enabled(uart_irq, true);
    
    // 受信、エラー、タイムアウト割り込みを有効化（送信は動的制御）
    uart_get_hw(UART_ID)->imsc = UART_UARTIMSC_RXIM_BITS |    // 受信割り込み
                                  UART_UARTIMSC_RTIM_BITS |    // 受信タイムアウト
                                  UART_UARTIMSC_OEIM_BITS |    // オーバーランエラー
                                  UART_UARTIMSC_BEIM_BITS |    // ブレークエラー
                                  UART_UARTIMSC_PEIM_BITS |    // パリティエラー
                                  UART_UARTIMSC_FEIM_BITS;     // フレーミングエラー
                                  // 送信割り込みは送信開始時に動的に有効化
    
    printf("UART Bridge initialized: %d baud, %d data bits, %d stop bits\n", 
           config->baudrate, config->data_bits, config->stop_bits);
    
    return 0;
}

void uart_bridge_set_callback(uart_data_received_callback_t callback) {
    g_data_callback = callback;
}

// 送信完了コールバック設定関数は削除（DMAを使用するため）

// UART送信可能チェック（新規追加）
bool uart_bridge_is_writable(void) {
    return uart_is_writable(UART_ID);
}

// 1バイト送信（DMA使用時は不要だが、互換性のため維持）
bool uart_bridge_send_byte(uint8_t byte) {
    if (uart_is_writable(UART_ID)) {
        uart_putc_raw(UART_ID, byte);
        return true;
    }
    return false;
}

// 送信割り込み制御関数は削除（DMAを使用するため）

void uart_bridge_set_error_callback(uart_error_callback_t callback) {
    g_error_callback = callback;
}

uart_error_stats_t* uart_bridge_get_error_stats(void) {
    return &error_stats;
}

void uart_bridge_reset_error_stats(void) {
    error_stats.overrun_errors = 0;
    error_stats.break_errors = 0;
    error_stats.parity_errors = 0;
    error_stats.framing_errors = 0;
    error_stats.timeout_errors = 0;
    error_stats.total_errors = 0;
    error_stats.consecutive_errors = 0;
    error_stats.max_consecutive_errors = 0;
    error_stats.total_bytes_received = 0;
    error_stats.valid_bytes_received = 0;
    
    // 送信関連デバッグカウンターは削除（DMAを使用するため）
}

void uart_bridge_print_error_stats(void) {
    printf("\n=== UART Statistics (DMA Mode) ===\n");
    printf("Total bytes received:   %lu\n", error_stats.total_bytes_received);
    printf("Valid bytes received:   %lu\n", error_stats.valid_bytes_received);
    printf("Bytes discarded:        %lu\n", error_stats.total_bytes_received - error_stats.valid_bytes_received);
    printf("--- Error Statistics ---\n");
    printf("Overrun errors:         %lu\n", error_stats.overrun_errors);
    printf("Break errors:           %lu\n", error_stats.break_errors);
    printf("Parity errors:          %lu\n", error_stats.parity_errors);
    printf("Framing errors:         %lu\n", error_stats.framing_errors);
    printf("Timeout errors:         %lu\n", error_stats.timeout_errors);
    printf("Total errors:           %lu\n", error_stats.total_errors);
    printf("Consecutive errors:     %lu\n", error_stats.consecutive_errors);
    printf("Max consecutive:        %lu\n", error_stats.max_consecutive_errors);
    printf("--- TX Info ---\n");
    printf("TX Mode:                DMA Transfer\n");
    printf("=======================\n\n");
}

void uart_bridge_set_config(uart_config_t *config) {
    if (!config) {
        return;
    }
    
    // UART設定を更新
    uart_set_baudrate(UART_ID, config->baudrate);
    uart_set_format(UART_ID, config->data_bits, config->stop_bits, (uart_parity_t)config->parity);
    
    const char *parity_str = (config->parity == 2) ? "Odd" :
                             (config->parity == 1) ? "Even" : "None";
    printf("UART config updated: %lu baud, %d data bits, %s parity, %d stop bits\n", 
           config->baudrate, config->data_bits, parity_str, config->stop_bits);
}

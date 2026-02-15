#ifndef UART_BRIDGE_H
#define UART_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/uart.h"
#include "common_types.h"
#include "pin_config.h"

// UART設定
typedef struct {
    uint32_t baudrate;
    uint8_t data_bits;
    uint8_t stop_bits;
    uint8_t parity;  // uart_parity_t -> uint8_t
} uart_config_t;

// UARTエラー種別
typedef enum {
    UART_ERROR_NONE = 0,
    UART_ERROR_OVERRUN = 1,
    UART_ERROR_BREAK = 2,
    UART_ERROR_PARITY = 4,
    UART_ERROR_FRAMING = 8,
    UART_ERROR_TIMEOUT = 16
} uart_error_type_t;

// UARTエラー統計
typedef struct {
    uint32_t overrun_errors;
    uint32_t break_errors;
    uint32_t parity_errors;
    uint32_t framing_errors;
    uint32_t timeout_errors;
    uint32_t total_errors;
    uint32_t consecutive_errors;
    uint32_t max_consecutive_errors;
    // 受信データ統計
    uint32_t total_bytes_received;      // 受信データ総バイト数（エラーで破棄したバイトも含む）
    uint32_t valid_bytes_received;      // エラーなく受信したバイト数（リングバッファに格納したバイト）
} uart_error_stats_t;

// コールバック関数型定義
typedef void (*uart_data_received_callback_t)(uint8_t *data, uint16_t length);
typedef void (*uart_error_callback_t)(uart_error_type_t error_type, uint32_t error_count);
// 送信完了コールバックは削除（DMAを使用するため）

// 公開関数
int uart_bridge_init(uart_config_t *config);
bool uart_bridge_is_writable(void);  // DMA使用時は不要だが互換性のため維持
bool uart_bridge_send_byte(uint8_t byte);  // DMA使用時は不要だが互換性のため維持
// 送信割り込み制御関数は削除（DMAを使用するため）
void uart_bridge_set_config(uart_config_t *config);
void uart_bridge_set_callback(uart_data_received_callback_t callback);
void uart_bridge_set_error_callback(uart_error_callback_t callback);
// 送信完了コールバック設定関数は削除（DMAを使用するため）
uart_error_stats_t* uart_bridge_get_error_stats(void);
void uart_bridge_reset_error_stats(void);
void uart_bridge_print_error_stats(void);

#endif // UART_BRIDGE_H
#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/uart.h"

// UARTパリティ定義（RP2040実際のハードウェア定義に合わせて修正）
// 既に定義されている場合は重複を避ける
#ifndef UART_PARITY_NONE
#define UART_PARITY_NONE 0
#define UART_PARITY_EVEN 1  // 実際のハードウェアでは1が偶数パリティ
#define UART_PARITY_ODD  2  // 実際のハードウェアでは2が奇数パリティ
#endif

// システム状態
typedef struct {
    bool bt_connected;
    bool uart_initialized;
    uint32_t uptime_seconds;
    uint32_t watchdog_reset_count;
} system_state_t;

// 設定データ
typedef struct {
    uint32_t uart_baudrate;
    uint8_t uart_data_bits;
    uint8_t uart_stop_bits;
    uint8_t uart_parity;
    uint32_t watchdog_timeout_ms;
    char device_name[32];
    char pin_code[5];
    bool debug_logging_enabled;
} system_config_t;

// データバッファ
#define BUFFER_SIZE 1024

typedef struct {
    uint8_t data[BUFFER_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    bool overflow;
} circular_buffer_t;

// エラーコード
typedef enum {
    ERROR_NONE = 0,
    ERROR_BT_CONNECTION_FAILED,
    ERROR_UART_INIT_FAILED,
    ERROR_BUFFER_OVERFLOW,
    ERROR_WATCHDOG_TIMEOUT,
    ERROR_SYSTEM_FAULT
} error_code_t;

#endif // COMMON_TYPES_H
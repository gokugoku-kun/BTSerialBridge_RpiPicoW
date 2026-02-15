#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "common_types.h"

// 設定保存結果
typedef enum {
    CONFIG_SAVE_SUCCESS = 0,
    CONFIG_SAVE_ERROR_FLASH,
    CONFIG_SAVE_ERROR_INVALID_DATA
} config_save_result_t;

// 設定読み込み結果
typedef enum {
    CONFIG_LOAD_SUCCESS = 0,
    CONFIG_LOAD_ERROR_NOT_FOUND,
    CONFIG_LOAD_ERROR_CORRUPTED,
    CONFIG_LOAD_ERROR_FLASH
} config_load_result_t;

// UART設定のフラッシュ保存用構造体
typedef struct {
    uint32_t uart_baudrate;
    uint8_t  uart_data_bits;
    uint8_t  uart_stop_bits;
    uint8_t  uart_parity;
    uint8_t  reserved;
} uart_flash_config_t;

// 公開関数
config_save_result_t config_manager_save(system_config_t *config);
config_load_result_t config_manager_load(system_config_t *config);

// UART設定のみをフラッシュに保存/読み出し
config_save_result_t config_manager_save_uart(const system_config_t *config);
config_load_result_t config_manager_load_uart(system_config_t *config);
void config_manager_reset_to_defaults(system_config_t *config);
bool config_manager_validate(system_config_t *config);
void config_manager_print_config(system_config_t *config);
void config_manager_apply_preset_debug_on(system_config_t *config);
void config_manager_apply_preset_debug_off(system_config_t *config);

// SW状態に応じたUART設定プリセットを適用
// sw_state: bit0=SW1, bit1=SW2 (switch_reader_get_state()の戻り値)
void config_manager_apply_sw_uart_preset(system_config_t *config, uint8_t sw_state);

#endif // CONFIG_MANAGER_H
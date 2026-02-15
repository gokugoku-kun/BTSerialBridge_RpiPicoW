#include "config_manager.h"
#include "flash_storage.h"
#include "status_manager.h"
#include "hardware/uart.h"
#include <stdio.h>
#include <string.h>

// デフォルト設定
static const system_config_t default_config = {
    .uart_baudrate = 115200,
    .uart_data_bits = 8,
    .uart_stop_bits = 1,
    .uart_parity = 0,  // UART_PARITY_NONE
    .watchdog_timeout_ms = 10000,
    .device_name = "BT Serial Bridge",
    .pin_code = "0000",
    .debug_logging_enabled = true
};

// 設定の妥当性チェック
bool config_manager_validate(system_config_t *config) {
    if (!config) {
        return false;
    }
    
    // ボーレートチェック
    if (config->uart_baudrate < 1200 || config->uart_baudrate > 921600) {
        status_manager_log(LOG_LEVEL_ERROR, "Invalid baudrate: %d", config->uart_baudrate);
        return false;
    }
    
    // データビットチェック
    if (config->uart_data_bits < 5 || config->uart_data_bits > 8) {
        status_manager_log(LOG_LEVEL_ERROR, "Invalid data bits: %d", config->uart_data_bits);
        return false;
    }
    
    // ストップビットチェック
    if (config->uart_stop_bits < 1 || config->uart_stop_bits > 2) {
        status_manager_log(LOG_LEVEL_ERROR, "Invalid stop bits: %d", config->uart_stop_bits);
        return false;
    }
    
    // パリティチェック
    if (config->uart_parity > 2) {  // UART_PARITY_EVEN
        status_manager_log(LOG_LEVEL_ERROR, "Invalid parity: %d", config->uart_parity);
        return false;
    }
    
    // ウォッチドッグタイムアウトチェック
    if (config->watchdog_timeout_ms < 1000 || config->watchdog_timeout_ms > 60000) {
        status_manager_log(LOG_LEVEL_ERROR, "Invalid watchdog timeout: %d", config->watchdog_timeout_ms);
        return false;
    }
    
    // デバイス名の長さチェック
    if (strlen(config->device_name) == 0 || strlen(config->device_name) >= sizeof(config->device_name)) {
        status_manager_log(LOG_LEVEL_ERROR, "Invalid device name length");
        return false;
    }
    
    // PINコードの長さチェック
    if (strlen(config->pin_code) != 4) {
        status_manager_log(LOG_LEVEL_ERROR, "Invalid PIN code length");
        return false;
    }
    
    return true;
}

// デフォルト設定にリセット
void config_manager_reset_to_defaults(system_config_t *config) {
    if (!config) {
        return;
    }
    
    *config = default_config;
    status_manager_log(LOG_LEVEL_INFO, "Configuration reset to defaults");
}

// 設定をフラッシュに保存
config_save_result_t config_manager_save(system_config_t *config) {
    if (!config || !config_manager_validate(config)) {
        return CONFIG_SAVE_ERROR_INVALID_DATA;
    }

    flash_result_t result = flash_storage_write(config, sizeof(system_config_t));
    if (result != FLASH_OK) {
        status_manager_log(LOG_LEVEL_ERROR, "Flash write failed: %d", result);
        return CONFIG_SAVE_ERROR_FLASH;
    }

    status_manager_log(LOG_LEVEL_INFO, "Configuration saved to flash");
    return CONFIG_SAVE_SUCCESS;
}

// UART設定のバリデーション（内部用）
static bool validate_uart_flash_config(const uart_flash_config_t *uart_cfg) {
    if (uart_cfg->uart_baudrate < 1200 || uart_cfg->uart_baudrate > 921600) return false;
    if (uart_cfg->uart_data_bits < 5 || uart_cfg->uart_data_bits > 8) return false;
    if (uart_cfg->uart_stop_bits < 1 || uart_cfg->uart_stop_bits > 2) return false;
    if (uart_cfg->uart_parity > 2) return false;
    return true;
}

// UART設定のみをフラッシュに保存
config_save_result_t config_manager_save_uart(const system_config_t *config) {
    if (!config) {
        return CONFIG_SAVE_ERROR_INVALID_DATA;
    }

    uart_flash_config_t uart_cfg = {
        .uart_baudrate  = config->uart_baudrate,
        .uart_data_bits = config->uart_data_bits,
        .uart_stop_bits = config->uart_stop_bits,
        .uart_parity    = config->uart_parity,
        .reserved       = 0
    };

    if (!validate_uart_flash_config(&uart_cfg)) {
        status_manager_log(LOG_LEVEL_ERROR, "Invalid UART config for save");
        return CONFIG_SAVE_ERROR_INVALID_DATA;
    }

    flash_result_t result = flash_storage_write(&uart_cfg, sizeof(uart_flash_config_t));
    if (result != FLASH_OK) {
        status_manager_log(LOG_LEVEL_ERROR, "Flash write failed: %d", result);
        return CONFIG_SAVE_ERROR_FLASH;
    }

    const char *parity_str = (uart_cfg.uart_parity == UART_PARITY_ODD) ? "O" :
                             (uart_cfg.uart_parity == UART_PARITY_EVEN) ? "E" : "N";
    status_manager_log(LOG_LEVEL_INFO, "UART config saved: %lu-%d-%s-%d",
        uart_cfg.uart_baudrate, uart_cfg.uart_data_bits,
        parity_str, uart_cfg.uart_stop_bits);
    return CONFIG_SAVE_SUCCESS;
}

// UART設定のみをフラッシュから読み出し（他の設定は変更しない）
config_load_result_t config_manager_load_uart(system_config_t *config) {
    if (!config) {
        return CONFIG_LOAD_ERROR_FLASH;
    }

    uart_flash_config_t uart_cfg;
    flash_result_t result = flash_storage_read(&uart_cfg, sizeof(uart_flash_config_t));
    switch (result) {
        case FLASH_OK:
            if (!validate_uart_flash_config(&uart_cfg)) {
                status_manager_log(LOG_LEVEL_ERROR, "Flash UART config validation failed");
                return CONFIG_LOAD_ERROR_CORRUPTED;
            }
            // UART関連フィールドのみ上書き
            config->uart_baudrate  = uart_cfg.uart_baudrate;
            config->uart_data_bits = uart_cfg.uart_data_bits;
            config->uart_stop_bits = uart_cfg.uart_stop_bits;
            config->uart_parity    = uart_cfg.uart_parity;

            status_manager_log(LOG_LEVEL_INFO, "UART config loaded from flash");
            return CONFIG_LOAD_SUCCESS;
        case FLASH_ERROR_NOT_FOUND:
            status_manager_log(LOG_LEVEL_INFO, "No saved UART config in flash");
            return CONFIG_LOAD_ERROR_NOT_FOUND;
        case FLASH_ERROR_CORRUPTED:
            status_manager_log(LOG_LEVEL_ERROR, "Flash UART config corrupted");
            return CONFIG_LOAD_ERROR_CORRUPTED;
        default:
            status_manager_log(LOG_LEVEL_ERROR, "Flash read error: %d", result);
            return CONFIG_LOAD_ERROR_FLASH;
    }
}

// 設定をフラッシュから読み込み
config_load_result_t config_manager_load(system_config_t *config) {
    if (!config) {
        return CONFIG_LOAD_ERROR_FLASH;
    }

    flash_result_t result = flash_storage_read(config, sizeof(system_config_t));
    switch (result) {
        case FLASH_OK:
            if (!config_manager_validate(config)) {
                status_manager_log(LOG_LEVEL_ERROR, "Flash config validation failed");
                return CONFIG_LOAD_ERROR_CORRUPTED;
            }
            status_manager_log(LOG_LEVEL_INFO, "Configuration loaded from flash");
            return CONFIG_LOAD_SUCCESS;
        case FLASH_ERROR_NOT_FOUND:
            status_manager_log(LOG_LEVEL_INFO, "No saved configuration in flash");
            return CONFIG_LOAD_ERROR_NOT_FOUND;
        case FLASH_ERROR_CORRUPTED:
            status_manager_log(LOG_LEVEL_ERROR, "Flash config corrupted");
            return CONFIG_LOAD_ERROR_CORRUPTED;
        default:
            status_manager_log(LOG_LEVEL_ERROR, "Flash read error: %d", result);
            return CONFIG_LOAD_ERROR_FLASH;
    }
}
void config_manager_print_config(system_config_t *config) {
    if (!config) {
        return;
    }
    
    status_manager_log(LOG_LEVEL_INFO, "=== Current Configuration ===");
    status_manager_log(LOG_LEVEL_INFO, "UART Baudrate: %d", config->uart_baudrate);
    status_manager_log(LOG_LEVEL_INFO, "UART Data bits: %d", config->uart_data_bits);
    status_manager_log(LOG_LEVEL_INFO, "UART Stop bits: %d", config->uart_stop_bits);
    status_manager_log(LOG_LEVEL_INFO, "UART Parity: %d", config->uart_parity);
    status_manager_log(LOG_LEVEL_INFO, "Watchdog timeout: %d ms", config->watchdog_timeout_ms);
    status_manager_log(LOG_LEVEL_INFO, "Device name: %s", config->device_name);
    status_manager_log(LOG_LEVEL_INFO, "PIN code: %s", config->pin_code);
    status_manager_log(LOG_LEVEL_INFO, "Debug logging: %s", 
                      config->debug_logging_enabled ? "enabled" : "disabled");
    status_manager_log(LOG_LEVEL_INFO, "============================");
}

void config_manager_apply_preset_debug_on(system_config_t *config) {
    if (!config) return;
    config->debug_logging_enabled = true;
    status_manager_log(LOG_LEVEL_INFO, "Debug logging enabled");
}

void config_manager_apply_preset_debug_off(system_config_t *config) {
    if (!config) return;
    config->debug_logging_enabled = false;
    status_manager_log(LOG_LEVEL_INFO, "Debug logging disabled");
}

// SW状態に応じたUART設定プリセットを適用
// SW2:SW1  設定
//   0:0    115200-8-N-1
//   0:1      9600-8-N-1
//   1:0     19200-8-O-1
//   1:1    230400-8-N-1
void config_manager_apply_sw_uart_preset(system_config_t *config, uint8_t sw_state) {
    if (!config) return;

    switch (sw_state & 0x03) {
        case 0x00:
            config->uart_baudrate  = 115200;
            config->uart_parity    = UART_PARITY_NONE;
            break;
        case 0x01:
            config->uart_baudrate  = 9600;
            config->uart_parity    = UART_PARITY_NONE;
            break;
        case 0x02:
            config->uart_baudrate  = 19200;
            config->uart_parity    = UART_PARITY_ODD;
            break;
        case 0x03:
            // SW=11: 不揮発ROM設定を使用（system_manager側で処理）
            status_manager_log(LOG_LEVEL_INFO,
                "SW preset: SW=0x03 -> flash config (handled by system_manager)");
            return;
    }
    config->uart_data_bits = 8;
    config->uart_stop_bits = 1;

    const char *parity_str = (config->uart_parity == UART_PARITY_ODD) ? "O" :
                             (config->uart_parity == UART_PARITY_EVEN) ? "E" : "N";
    status_manager_log(LOG_LEVEL_INFO,
        "SW preset applied (SW=0x%02X): %lu-%d-%s-%d",
        sw_state & 0x03,
        config->uart_baudrate,
        config->uart_data_bits,
        parity_str,
        config->uart_stop_bits);
}
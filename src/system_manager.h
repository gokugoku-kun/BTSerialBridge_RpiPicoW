#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include "common_types.h"

// システム初期化結果
typedef enum {
    SYSTEM_INIT_SUCCESS = 0,
    SYSTEM_INIT_ERROR_CYW43,
    SYSTEM_INIT_ERROR_STATUS_MANAGER,
    SYSTEM_INIT_ERROR_WATCHDOG,
    SYSTEM_INIT_ERROR_UART,
    SYSTEM_INIT_ERROR_BLUETOOTH,
    SYSTEM_INIT_ERROR_DATA_TRANSFER
} system_init_result_t;

// 公開関数
system_init_result_t system_manager_init(void);
void system_manager_process(void);
system_state_t* system_manager_get_state(void);
system_config_t* system_manager_get_config(void);
void system_manager_print_status(void);
void system_manager_print_uart_config(void);
void system_manager_shutdown(void);

// UART設定対話メニュー（ブロッキング）
void system_manager_uart_config_interactive(void);

#endif // SYSTEM_MANAGER_H
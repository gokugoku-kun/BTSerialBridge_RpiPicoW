#ifndef STATUS_MANAGER_H
#define STATUS_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

// LED状態
typedef enum {
    LED_STATE_PAIRING_WAIT,    // 点滅（遅い）
    LED_STATE_CONNECTED,       // 点灯
    LED_STATE_DATA_TRANSFER,   // 点滅（速い）
    LED_STATE_ERROR           // 点滅（エラーパターン）
} led_state_t;

// ログレベル
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} log_level_t;

// 公開関数
int status_manager_init(void);
void status_manager_set_led_state(led_state_t state);
void status_manager_log(log_level_t level, const char *format, ...);
void status_manager_set_log_level(log_level_t level);
void status_manager_process(void);

#endif // STATUS_MANAGER_H
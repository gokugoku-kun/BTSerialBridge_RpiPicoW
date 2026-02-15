#ifndef WATCHDOG_MANAGER_H
#define WATCHDOG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

// ウォッチドッグ設定
typedef struct {
    uint32_t timeout_ms;
    uint32_t reset_count_threshold;
} watchdog_config_t;

// 公開関数
int watchdog_manager_init(watchdog_config_t *config);
void watchdog_manager_feed(void);
uint32_t watchdog_manager_get_reset_count(void);
bool watchdog_manager_is_enabled(void);
void watchdog_manager_disable(void);
void watchdog_manager_force_reset(void);
void watchdog_manager_print_diagnostics(void);
void watchdog_manager_reset_stats(void);

#endif // WATCHDOG_MANAGER_H
#include "watchdog_manager.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "pico/stdlib.h"
#include <stdio.h>

static watchdog_config_t g_config = {0};
static uint32_t reset_count = 0;
static bool watchdog_initialized = false;
static uint32_t last_feed_time = 0;
static uint32_t feed_count = 0;

// リセット原因を確認してリセット回数を更新
static void check_reset_reason() {
    // ウォッチドッグリセットかどうかを確認
    if (watchdog_caused_reboot()) {
        reset_count++;
        printf("Watchdog reset detected. Reset count: %d\n", reset_count);
        
        // リセット回数が閾値を超えた場合の処理
        if (g_config.reset_count_threshold > 0 && 
            reset_count >= g_config.reset_count_threshold) {
            printf("WARNING: Watchdog reset count exceeded threshold (%d)\n", 
                   g_config.reset_count_threshold);
        }
    } else {
        printf("Normal boot detected\n");
    }
}

int watchdog_manager_init(watchdog_config_t *config) {
    if (!config || config->timeout_ms == 0) {
        printf("Invalid watchdog config\n");
        return -1;
    }
    
    // 設定をコピー
    g_config = *config;
    
    // リセット原因を確認
    check_reset_reason();
    
    // ウォッチドッグタイマを有効化
    watchdog_enable(config->timeout_ms, true);
    
    watchdog_initialized = true;
    
    printf("Watchdog Manager initialized: timeout=%dms, threshold=%d\n", 
           config->timeout_ms, config->reset_count_threshold);
    
    return 0;
}

void watchdog_manager_feed() {
    if (!watchdog_initialized) {
        return;
    }
    
    watchdog_update();
    last_feed_time = to_ms_since_boot(get_absolute_time());
    feed_count++;
}

uint32_t watchdog_manager_get_reset_count() {
    return reset_count;
}

// ウォッチドッグの状態を取得
bool watchdog_manager_is_enabled() {
    return watchdog_initialized;
}

// ウォッチドッグタイマを無効化（テスト用）
void watchdog_manager_disable() {
    if (watchdog_initialized) {
        // Pico SDKにはwatchdog_disableがないため、
        // 非常に長いタイムアウトを設定することで実質的に無効化
        watchdog_enable(0x7fffff, false); // 最大値に近い値
        watchdog_initialized = false;
        printf("Watchdog disabled\n");
    }
}

// 強制的にウォッチドッグリセットを発生させる（テスト用）
void watchdog_manager_force_reset() {
    printf("Forcing watchdog reset...\n");
    while (true) {
        // ウォッチドッグをフィードしないでループ
        sleep_ms(100);
    }
}

// 診断情報を出力
void watchdog_manager_print_diagnostics() {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    uint32_t time_since_last_feed = current_time - last_feed_time;
    
    printf("=== Watchdog Diagnostics ===\n");
    printf("Initialized: %s\n", watchdog_initialized ? "Yes" : "No");
    printf("Timeout: %d ms\n", g_config.timeout_ms);
    printf("Reset count threshold: %d\n", g_config.reset_count_threshold);
    printf("Total reset count: %d\n", reset_count);
    printf("Feed count: %d\n", feed_count);
    printf("Last feed time: %d ms\n", last_feed_time);
    printf("Time since last feed: %d ms\n", time_since_last_feed);
    
    if (time_since_last_feed > (g_config.timeout_ms * 0.8)) {
        printf("WARNING: Time since last feed is close to timeout!\n");
    }
    
    printf("============================\n");
}

// フィード統計をリセット
void watchdog_manager_reset_stats() {
    feed_count = 0;
    last_feed_time = to_ms_since_boot(get_absolute_time());
    printf("Watchdog statistics reset\n");
}
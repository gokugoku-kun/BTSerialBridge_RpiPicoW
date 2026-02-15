#include "status_manager.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include <stdio.h>

static led_state_t current_led_state = LED_STATE_PAIRING_WAIT;
static uint32_t led_timer = 0;
static bool led_on = false;
static log_level_t min_log_level = LOG_LEVEL_INFO; // 最小ログレベル

// LED点滅パターンの定義（ms）
#define LED_PAIRING_WAIT_PERIOD 1000    // 1秒間隔で点滅 - ペアリング待機
#define LED_BT_PREPARING_PERIOD 500     // 500ms間隔で点滅 - BT内部準備中
#define LED_CONNECTED_PERIOD 0          // 常時点灯 - 接続完了・送信可能
#define LED_DATA_TRANSFER_PERIOD 100    // 100ms間隔で点滅 - データ転送中
#define LED_ERROR_PERIOD 200            // 200ms間隔で点滅 - エラー

static uint32_t get_led_period(led_state_t state) {
    switch (state) {
        case LED_STATE_PAIRING_WAIT:
            return LED_PAIRING_WAIT_PERIOD;
        case LED_STATE_CONNECTED:
            return LED_CONNECTED_PERIOD;
        case LED_STATE_DATA_TRANSFER:
            return LED_DATA_TRANSFER_PERIOD;
        case LED_STATE_ERROR:
            return LED_ERROR_PERIOD;
        default:
            return LED_PAIRING_WAIT_PERIOD;
    }
}

int status_manager_init(void) {
    // CYW43は既にsystem_manager_init()で初期化済みなので、
    // ここでは初期化チェックのみ行う
    
    // 初期状態設定
    current_led_state = LED_STATE_PAIRING_WAIT;
    led_timer = 0;
    led_on = false;
    
    // LEDを初期状態に設定
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
    
    printf("Status Manager initialized\n");
    return 0;
}

void status_manager_set_led_state(led_state_t state) {
    if (current_led_state != state) {
        current_led_state = state;
        led_timer = 0; // タイマーリセット
        
        printf("LED state changed to: ");
        switch (state) {
            case LED_STATE_PAIRING_WAIT:
                printf("PAIRING_WAIT\n");
                break;
            case LED_STATE_CONNECTED:
                printf("CONNECTED\n");
                led_on = true;
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
                break;
            case LED_STATE_DATA_TRANSFER:
                printf("DATA_TRANSFER\n");
                break;
            case LED_STATE_ERROR:
                printf("ERROR\n");
                break;
        }
    }
}

void status_manager_process(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint32_t period = get_led_period(current_led_state);
    
    // 常時点灯の場合は処理不要
    if (period == 0) {
        return;
    }
    
    // 点滅処理
    if (now - led_timer >= period) {
        led_timer = now;
        led_on = !led_on;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
    }
}

void status_manager_log(log_level_t level, const char *format, ...) {
    // ログレベルフィルタリング
    if (level < min_log_level) {
        return;
    }
    
    const char *level_str;
    switch (level) {
        case LOG_LEVEL_DEBUG:
            level_str = "DEBUG";
            break;
        case LOG_LEVEL_INFO:
            level_str = "INFO";
            break;
        case LOG_LEVEL_WARN:
            level_str = "WARN";
            break;
        case LOG_LEVEL_ERROR:
            level_str = "ERROR";
            break;
        default:
            level_str = "UNKNOWN";
            break;
    }
    
    // タイムスタンプ付きでログ出力
    uint32_t timestamp = to_ms_since_boot(get_absolute_time());
    printf("[%08d][%s] ", timestamp, level_str);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
}

void status_manager_set_log_level(log_level_t level) {
    min_log_level = level;
    status_manager_log(LOG_LEVEL_INFO, "Log level set to %d", level);
}
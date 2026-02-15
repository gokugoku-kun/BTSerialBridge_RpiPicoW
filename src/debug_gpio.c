#include "debug_gpio.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdio.h>

static bool debug_gpio_initialized = false;
static const uint8_t debug_gpio_pins[DEBUG_GPIO_COUNT] = {
    PIN_DEBUG_0,
    PIN_DEBUG_1,
    PIN_DEBUG_2
};

void debug_gpio_init(void) {
    if (debug_gpio_initialized) {
        return;
    }
    
    // 全てのデバッグGPIOを初期化
    for (int i = 0; i < DEBUG_GPIO_COUNT; i++) {
        gpio_init(debug_gpio_pins[i]);
        gpio_set_dir(debug_gpio_pins[i], GPIO_OUT);
        gpio_put(debug_gpio_pins[i], false);  // 初期状態はLOW
    }
    
    debug_gpio_initialized = true;
    printf("Debug GPIO initialized on pins %d, %d, %d\n", 
           PIN_DEBUG_0, PIN_DEBUG_1, PIN_DEBUG_2);
}

void debug_gpio_set(uint8_t pin_index, bool state) {
    if (!debug_gpio_initialized) {
        debug_gpio_init();
    }
    
    gpio_put(pin_index, state);
}

void debug_gpio_toggle(uint8_t pin_index) {
    if (!debug_gpio_initialized) {
        debug_gpio_init();
    }

    bool current_state = gpio_get(pin_index);
    gpio_put(pin_index, !current_state);
}

void debug_gpio_pulse(uint8_t pin_index, uint32_t duration_us) {
    if (!debug_gpio_initialized) {
        debug_gpio_init();
    }

    // パルス生成: HIGH → 指定時間待機 → LOW
    gpio_put(pin_index, true);
    busy_wait_us(duration_us);
    gpio_put(pin_index, false);
}
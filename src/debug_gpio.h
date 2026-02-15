#ifndef DEBUG_GPIO_H
#define DEBUG_GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include "pin_config.h"

#define DEBUG_GPIO_ENABLED

// デバッグ用GPIO関数
void debug_gpio_init(void);
void debug_gpio_set(uint8_t pin_index, bool state);
void debug_gpio_toggle(uint8_t pin_index);
void debug_gpio_pulse(uint8_t pin_index, uint32_t duration_us);

// デバッグ用マクロ（条件付きコンパイル対応）
#ifdef DEBUG_GPIO_ENABLED
    #define DEBUG_GPIO_HIGH()       debug_gpio_set_legacy(true)
    #define DEBUG_GPIO_LOW()        debug_gpio_set_legacy(false)
    #define DEBUG_GPIO_TOGGLE()     debug_gpio_toggle_legacy()
    #define DEBUG_GPIO_PULSE(us)    debug_gpio_pulse_legacy(us)
    
    // 新しいマルチピン対応マクロ
    #define DEBUG_GPIO_SET(pin, state)    debug_gpio_set(pin, state)
    #define DEBUG_GPIO_TOGGLE_PIN(pin)    debug_gpio_toggle(pin)
    #define DEBUG_GPIO_PULSE_PIN(pin, us) debug_gpio_pulse(pin, us)
    #define DEBUG_GPIO_PATTERN(pattern)   debug_gpio_set_pattern(pattern)
#else
    #define DEBUG_GPIO_HIGH()       do {} while(0)
    #define DEBUG_GPIO_LOW()        do {} while(0)
    #define DEBUG_GPIO_TOGGLE()     do {} while(0)
    #define DEBUG_GPIO_PULSE(us)    do {} while(0)
    
    #define DEBUG_GPIO_SET(pin, state)    do {} while(0)
    #define DEBUG_GPIO_TOGGLE_PIN(pin)    do {} while(0)
    #define DEBUG_GPIO_PULSE_PIN(pin, us) do {} while(0)
    #define DEBUG_GPIO_PATTERN(pattern)   do {} while(0)
#endif

#endif // DEBUG_GPIO_H
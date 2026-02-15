#include "switch_reader.h"
#include "hardware/gpio.h"
#include <stdio.h>

// ノイズフィルタ設定
#define FILTER_MATCH_COUNT  3   // 一致必要回数
#define FILTER_RETRY_MAX    10  // 最大リトライ回数

// 内部保持するSW確定状態
static uint8_t sw_state = 0;

// 1つのGPIOに対して3回一致フィルタ付きで読み取る
// 戻り値: 確定したGPIOレベル (true=High, false=Low)
static bool read_gpio_filtered(uint gpio_pin) {
    for (int retry = 0; retry < FILTER_RETRY_MAX; retry++) {
        bool first = gpio_get(gpio_pin);
        bool matched = true;

        for (int i = 1; i < FILTER_MATCH_COUNT; i++) {
            if (gpio_get(gpio_pin) != first) {
                matched = false;
                break;
            }
        }

        if (matched) {
            return first;
        }
    }

    // リトライ上限到達時は多数決で判定
    int high_count = 0;
    for (int i = 0; i < FILTER_MATCH_COUNT; i++) {
        if (gpio_get(gpio_pin)) {
            high_count++;
        }
    }
    return (high_count > (FILTER_MATCH_COUNT / 2));
}

void switch_reader_init(void) {
    // GP20(SW2), GP21(SW1) を入力モードで初期化（外部プルアップ）
    gpio_init(PIN_SW1);
    gpio_set_dir(PIN_SW1, GPIO_IN);

    gpio_init(PIN_SW2);
    gpio_set_dir(PIN_SW2, GPIO_IN);

    // 初回読み取り
    switch_reader_update();

    printf("Switch reader initialized (SW1=GP%d, SW2=GP%d, state=0x%02X)\n",
           PIN_SW1, PIN_SW2, sw_state);
}

uint8_t switch_reader_get_state(void) {
    return sw_state;
}

void switch_reader_update(void) {
    uint8_t new_state = 0;

    // GPIO Low = SW ON なので反転して格納
    if (!read_gpio_filtered(PIN_SW1)) {
        new_state |= SW1_BIT;
    }
    if (!read_gpio_filtered(PIN_SW2)) {
        new_state |= SW2_BIT;
    }

    sw_state = new_state;
}

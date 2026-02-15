#ifndef SWITCH_READER_H
#define SWITCH_READER_H

#include <stdint.h>
#include "pin_config.h"

// スイッチビットマスク定義
#define SW1_BIT (1 << 0)  // bit0: SW1
#define SW2_BIT (1 << 1)  // bit1: SW2

// 初期化（GPIO設定 + 初回SW状態読み取り）
void switch_reader_init(void);

// SW状態取得（bit0=SW1, bit1=SW2, ON=1, OFF=0）
uint8_t switch_reader_get_state(void);

// SW状態を再読み取りして内部値を更新
void switch_reader_update(void);

#endif // SWITCH_READER_H

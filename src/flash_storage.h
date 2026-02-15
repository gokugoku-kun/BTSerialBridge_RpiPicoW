#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// フラッシュ操作結果
typedef enum {
    FLASH_OK = 0,
    FLASH_ERROR_NOT_FOUND,      // マジックナンバー不一致（未保存）
    FLASH_ERROR_CORRUPTED,      // CRC不一致
    FLASH_ERROR_WRITE_FAILED,   // 書き込み失敗
    FLASH_ERROR_INVALID_PARAM   // 引数不正
} flash_result_t;

// 初期化
void flash_storage_init(void);

// データ書き込み（セクタ消去→書き込み、割り込み排他付き）
flash_result_t flash_storage_write(const void *data, size_t size);

// データ読み出し（マジックナンバー・CRC検証付き）
flash_result_t flash_storage_read(void *data, size_t size);

// 保存領域を消去
flash_result_t flash_storage_erase(void);

// 保存データが存在するか確認
bool flash_storage_has_valid_data(void);

#endif // FLASH_STORAGE_H

#include "flash_storage.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>
#include <stdio.h>

// フラッシュ末尾4KBセクタを使用
#define FLASH_CONFIG_OFFSET  (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_MAGIC          0x42545301  // "BTS\x01"
#define FLASH_DATA_VERSION   1

// 最大保存データサイズ（ヘッダ除くペイロード上限）
#define FLASH_MAX_PAYLOAD    (FLASH_PAGE_SIZE - sizeof(flash_header_t))

// フラッシュ保存ヘッダ
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint16_t data_size;
    uint16_t reserved;
    uint32_t crc32;
} flash_header_t;

// XIP経由の読み出しポインタ
static const uint8_t *flash_target =
    (const uint8_t *)(XIP_BASE + FLASH_CONFIG_OFFSET);

// CRC32計算（ビット演算方式、テーブル不要で省メモリ）
static uint32_t calc_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

void flash_storage_init(void) {
    printf("Flash storage: offset=0x%08X, sector=%dB, page=%dB\n",
           FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE, FLASH_PAGE_SIZE);
}

flash_result_t flash_storage_write(const void *data, size_t size) {
    if (!data || size == 0 || size > FLASH_MAX_PAYLOAD) {
        return FLASH_ERROR_INVALID_PARAM;
    }

    // ページバッファ構築（256バイト境界）
    uint8_t page_buf[FLASH_PAGE_SIZE] __attribute__((aligned(4)));
    memset(page_buf, 0xFF, sizeof(page_buf));

    // ヘッダ設定
    flash_header_t *header = (flash_header_t *)page_buf;
    header->magic     = FLASH_MAGIC;
    header->version   = FLASH_DATA_VERSION;
    header->data_size = (uint16_t)size;
    header->reserved  = 0;

    // ペイロードコピー
    memcpy(page_buf + sizeof(flash_header_t), data, size);

    // CRC計算（ペイロード部分のみ）
    header->crc32 = calc_crc32(page_buf + sizeof(flash_header_t), size);

    // 割り込み排他でフラッシュ書き込み
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CONFIG_OFFSET, page_buf, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    // ベリファイ
    if (memcmp(flash_target, page_buf, sizeof(flash_header_t) + size) != 0) {
        return FLASH_ERROR_WRITE_FAILED;
    }

    printf("Flash storage: wrote %d bytes\n", (int)size);
    return FLASH_OK;
}

flash_result_t flash_storage_read(void *data, size_t size) {
    if (!data || size == 0) {
        return FLASH_ERROR_INVALID_PARAM;
    }

    const flash_header_t *header = (const flash_header_t *)flash_target;

    // マジックナンバー検証
    if (header->magic != FLASH_MAGIC) {
        return FLASH_ERROR_NOT_FOUND;
    }

    // サイズ検証
    if (header->data_size != size) {
        return FLASH_ERROR_CORRUPTED;
    }

    // CRC検証
    const uint8_t *payload = flash_target + sizeof(flash_header_t);
    uint32_t expected_crc = calc_crc32(payload, header->data_size);
    if (header->crc32 != expected_crc) {
        return FLASH_ERROR_CORRUPTED;
    }

    memcpy(data, payload, size);

    printf("Flash storage: read %d bytes\n", (int)size);
    return FLASH_OK;
}

flash_result_t flash_storage_erase(void) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);

    printf("Flash storage: erased\n");
    return FLASH_OK;
}

bool flash_storage_has_valid_data(void) {
    const flash_header_t *header = (const flash_header_t *)flash_target;
    if (header->magic != FLASH_MAGIC) {
        return false;
    }
    const uint8_t *payload = flash_target + sizeof(flash_header_t);
    uint32_t expected_crc = calc_crc32(payload, header->data_size);
    return (header->crc32 == expected_crc);
}

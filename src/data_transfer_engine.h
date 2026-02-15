#ifndef DATA_TRANSFER_ENGINE_H
#define DATA_TRANSFER_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

// 転送統計
typedef struct {
    uint32_t bt_to_uart_bytes;
    uint32_t uart_to_bt_bytes;
    uint32_t transfer_errors;
    uint32_t max_transfer_delay_us;
    uint32_t bt_to_uart_packets;
    uint32_t uart_to_bt_packets;
    uint32_t buffer_overflow_errors;
    uint32_t connection_errors;
    uint32_t ring_buffer_overflows;      // 既存
    uint32_t max_interrupt_disable_us;   // 既存
    // DMA関連統計
    uint32_t dma_transfers;              // DMA転送回数
    uint32_t dma_bytes_transferred;      // DMA転送バイト数
    uint32_t dma_errors;                 // DMA転送エラー数
    uint32_t max_dma_transfer_time_us;   // 最大DMA転送時間
    uint32_t bus_conflicts;              // バス競合回数
    uint32_t buffer_swaps;               // バッファスワップ回数
} transfer_stats_t;

// 公開関数
int data_transfer_init(void);
void data_transfer_process(void);
transfer_stats_t* data_transfer_get_stats(void);
void data_transfer_reset_stats(void);
void data_transfer_print_stats(void);

#endif // DATA_TRANSFER_ENGINE_H
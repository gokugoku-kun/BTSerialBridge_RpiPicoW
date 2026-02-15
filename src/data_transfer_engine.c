/*
 * Data Transfer Engine - DMA Implementation
 * 
 * BT→UART送信をDMA転送とダブルバッファシステムで実装
 * - ダブルバッファによる連続データ処理
 * - DMA転送によるCPU負荷軽減
 * - UART FIFOフル活用による最大スループット
 * - バス競合軽減による効率向上
 * 
 * UART→BT送信は既存のリングバッファシステムを維持
 */

#include "data_transfer_engine.h"
#include "uart_bridge.h"
#include "bt_spp_handler.h"
#include "status_manager.h"
#include "pin_config.h"
#include "hardware/timer.h"
#include "hardware/uart.h"
#include "hardware/sync.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pico/critical_section.h"
#include <stdio.h>
#include <string.h>

// DMAバッファ設定
#define DMA_BUFFER_SIZE 1024  // 各バッファのサイズ（1KB）

// 32バイト境界アライメントされたダブルバッファ構造体
typedef struct {
    __attribute__((aligned(32))) uint8_t buffer_a[DMA_BUFFER_SIZE];
    __attribute__((aligned(32))) uint8_t buffer_b[DMA_BUFFER_SIZE];
    volatile uint8_t *write_buffer;    // 現在の書き込みバッファ
    volatile uint8_t *read_buffer;     // 現在の読み込みバッファ
    volatile uint16_t write_pos;       // 書き込み位置
    volatile uint16_t read_size;       // 読み込みサイズ
    volatile bool dma_in_progress;     // DMA転送中フラグ
    critical_section_t lock;           // 排他制御
} bt_uart_double_buffer_t;

// DMAマネージャー構造体
typedef struct {
    uint dma_channel;
    dma_channel_config config;
    volatile bool transfer_in_progress;
    volatile uint32_t bytes_transferred;
    volatile uint32_t transfer_errors;
    bool channel_claimed;              // チャネル取得状態
} bt_uart_dma_manager_t;

// DMA関連グローバル変数（32バイト境界アライメント）
__attribute__((aligned(32))) static bt_uart_double_buffer_t bt_to_uart_double_buffer;
static bt_uart_dma_manager_t bt_to_uart_dma_manager;

// リングバッファ設定（UART→BT送信用のみ維持）
#define RING_BUFFER_SIZE 2048
#define BT_MAX_BATCH_SIZE 244  // Bluetooth SPPの最大パケットサイズに合わせる

// リングバッファ構造体（UART→BT送信用のみ）
typedef struct {
    uint8_t data[RING_BUFFER_SIZE];
    volatile uint16_t write_pos;
    volatile uint16_t read_pos;
    volatile uint16_t count;
    volatile bool overflow;
} ring_buffer_t;

// リングバッファ（UART→BT送信用のみ）
static ring_buffer_t uart_to_bt_buffer;    // UART→BT用リングバッファ（維持）
// BT→UART用リングバッファは削除（ダブルバッファに置き換え）

static transfer_stats_t g_stats = {0};
static bool engine_initialized = false;

// 既存のUART送信関連変数（削除）
// static volatile bool uart_tx_in_progress = false;  // 削除済み
// static volatile uint32_t start_transmission_count = 0;  // 削除済み
// static volatile uint32_t tx_complete_count = 0;  // 削除済み

// 前方宣言
static void handle_dma_error(const char* error_msg);
static void bt_uart_dma_complete_handler(void);

// リングバッファ基本操作関数
static void ring_buffer_init(ring_buffer_t* buffer) {
    buffer->write_pos = 0;
    buffer->read_pos = 0;
    buffer->count = 0;
    buffer->overflow = false;
}

static uint16_t ring_buffer_write(ring_buffer_t* buffer, const uint8_t* data, uint16_t length) {
    uint16_t written = 0;
    
    for (uint16_t i = 0; i < length; i++) {
        if (buffer->count >= RING_BUFFER_SIZE) {
            // バッファ満杯時は古いデータを上書き
            buffer->read_pos = (buffer->read_pos + 1) % RING_BUFFER_SIZE;
            buffer->overflow = true;
            g_stats.ring_buffer_overflows++;  // 統計更新
        } else {
            buffer->count++;
        }
        
        buffer->data[buffer->write_pos] = data[i];
        buffer->write_pos = (buffer->write_pos + 1) % RING_BUFFER_SIZE;
        written++;
    }
    
    return written;
}

static uint16_t ring_buffer_peek(const ring_buffer_t* buffer, uint8_t* data, uint16_t max_length) {
    uint16_t available = buffer->count;
    uint16_t to_read = (available < max_length) ? available : max_length;
    uint16_t read_pos = buffer->read_pos;
    
    for (uint16_t i = 0; i < to_read; i++) {
        data[i] = buffer->data[read_pos];
        read_pos = (read_pos + 1) % RING_BUFFER_SIZE;
    }
    
    return to_read;
}

static void ring_buffer_consume(ring_buffer_t* buffer, uint16_t length) {
    uint16_t to_consume = (buffer->count < length) ? buffer->count : length;
    
    buffer->read_pos = (buffer->read_pos + to_consume) % RING_BUFFER_SIZE;
    buffer->count -= to_consume;
}

static uint16_t ring_buffer_available_data(const ring_buffer_t* buffer) {
    return buffer->count;
}

static uint16_t ring_buffer_available_space(const ring_buffer_t* buffer) {
    return RING_BUFFER_SIZE - buffer->count;
}

static void ring_buffer_clear(ring_buffer_t* buffer) {
    buffer->write_pos = 0;
    buffer->read_pos = 0;
    buffer->count = 0;
    buffer->overflow = false;
}

// ダブルバッファ関数群
static int bt_uart_double_buffer_init(bt_uart_double_buffer_t *db) {
    if (!db) {
        return -1;
    }
    
    // critical_sectionの初期化
    critical_section_init(&db->lock);
    
    // バッファポインタの初期設定
    db->write_buffer = db->buffer_a;
    db->read_buffer = db->buffer_b;
    
    // 位置とサイズの初期化
    db->write_pos = 0;
    db->read_size = 0;
    
    // 状態フラグの初期化
    db->dma_in_progress = false;
    
    // バッファをゼロクリア
    memset((void*)db->buffer_a, 0, DMA_BUFFER_SIZE);
    memset((void*)db->buffer_b, 0, DMA_BUFFER_SIZE);
    
    printf("Double buffer initialized: write=%p, read=%p\n", 
           (void*)db->write_buffer, (void*)db->read_buffer);
    
    return 0;
}

// ロック済み前提の内部関数
static int bt_uart_double_buffer_write_locked(bt_uart_double_buffer_t *db, const uint8_t *data, uint16_t length) {
    if (!db || !data || length == 0) {
        return -1;
    }
    
    // 既にロック取得済みと仮定
    
    // 書き込みバッファの容量チェック
    uint16_t available = DMA_BUFFER_SIZE - db->write_pos;
    if (length > available) {
        // バッファ満杯の場合はエラーを返す
        return -1;  // バッファオーバーフロー
    }
    
    // 高速メモリコピー（4バイト単位）
    uint8_t *dest = (uint8_t*)db->write_buffer + db->write_pos;
    
    // 4バイト境界にアライメントされている場合の高速コピー
    if (((uintptr_t)data % 4 == 0) && ((uintptr_t)dest % 4 == 0) && (length >= 4)) {
        uint32_t *src32 = (uint32_t*)data;
        uint32_t *dest32 = (uint32_t*)dest;
        uint16_t words = length / 4;
        uint16_t remaining = length % 4;
        
        // 4バイト単位での高速コピー
        for (uint16_t i = 0; i < words; i++) {
            dest32[i] = src32[i];
        }
        
        // 残りバイトを処理
        if (remaining > 0) {
            uint8_t *src8 = (uint8_t*)&src32[words];
            uint8_t *dest8 = (uint8_t*)&dest32[words];
            for (uint16_t i = 0; i < remaining; i++) {
                dest8[i] = src8[i];
            }
        }
    } else {
        // 通常のバイト単位コピー
        memcpy(dest, data, length);
    }
    
    db->write_pos += length;
    
    return length;
}

static void bt_uart_double_buffer_swap(bt_uart_double_buffer_t *db) {
    if (!db) {
        return;
    }
    
    // 既にロック取得済みと仮定
    // DMA転送中でない場合のみスワップ実行
    if (!db->dma_in_progress && db->write_pos > 0) {
        // アトミックなポインタ交換
        uint8_t *temp = (uint8_t*)db->write_buffer;
        db->write_buffer = db->read_buffer;
        db->read_buffer = temp;
        
        // 新しい読み込みバッファのサイズを設定
        db->read_size = db->write_pos;
        db->write_pos = 0;  // 新しい書き込みバッファをリセット
        
        // printf("Buffer swapped: write=%p, read=%p, read_size=%d\n", 
        //        (void*)db->write_buffer, (void*)db->read_buffer, db->read_size);
    }
}

static int bt_uart_double_buffer_prepare_dma_locked(bt_uart_double_buffer_t *db, uint8_t **buffer, uint16_t *size) {
    if (!db || !buffer || !size) {
        return -1;
    }
    
    // 既にロック取得済みと仮定
    
    // 読み込みバッファにデータがあるかチェック
    if (db->read_size == 0) {
        return -1;  // 転送するデータなし
    }
    
    // DMA転送用データを準備
    *buffer = (uint8_t*)db->read_buffer;
    *size = db->read_size;
    
    // メモリアライメントの確認
    if ((uintptr_t)*buffer % 4 != 0) {
        // printf("Warning: DMA buffer not 4-byte aligned: %p\n", (void*)*buffer);
    }
    
    // printf("DMA prepared: buffer=%p, size=%d\n", (void*)*buffer, *size);
    
    return 0;
}

// DMAマネージャー関数群
static int bt_uart_dma_claim_channel_optimized(void) {
    // 動的チャネル割り当てを第一選択として使用
    int channel = dma_claim_unused_channel(false);
    
    if (channel >= 0) {
        printf("DMA channel %d claimed for BT->UART transfer (bus optimized)\n", channel);
        return channel;
    }
    
    // 全チャネルが使用中の場合
    printf("No DMA channels available for BT->UART transfer\n");
    return -1;  // エラー: 利用可能なチャネルなし
}

static void configure_uart_fifo_for_dma(void) {
    uart_hw_t *uart_hw = uart_get_hw(UART_PERIPH);
    
    // 送信FIFO閾値を最大に設定（32バイト満杯まで使用）
    // 4バイト空きでDREQ発生（28/32バイト使用時）
    uart_hw->ifls = (uart_hw->ifls & ~UART_UARTIFLS_TXIFLSEL_BITS) |
                    (0 << UART_UARTIFLS_TXIFLSEL_LSB);  // 送信: 1/8 full (4 bytes空き時にDREQ)
    
    // FIFOを有効化
    uart_set_fifo_enabled(UART_PERIPH, true);
    
    printf("UART FIFO configured for DMA: TX threshold=4 bytes empty\n");
}

// DMA割り込みハンドラ
static void bt_uart_dma_irq_handler(void) {
    // 使用中のDMAチャネルの割り込み状態をチェック
    if (dma_channel_get_irq0_status(bt_to_uart_dma_manager.dma_channel)) {
        // 割り込みフラグをクリア
        dma_channel_acknowledge_irq0(bt_to_uart_dma_manager.dma_channel);
        
        // DMA完了処理を呼び出し
        bt_uart_dma_complete_handler();
    }
    // 注意: 他のDMAチャネルの割り込みは処理しない（専用ハンドラ）
}

static int bt_uart_dma_manager_init(bt_uart_dma_manager_t *dm) {
    if (!dm) {
        return -1;
    }
    
    // DMAチャネルを取得
    int channel = bt_uart_dma_claim_channel_optimized();
    if (channel < 0) {
        return -1;
    }
    
    dm->dma_channel = (uint)channel;
    dm->channel_claimed = true;
    
    // DMA設定の初期化
    dm->config = dma_channel_get_default_config(dm->dma_channel);
    
    // 基本的なDMA設定
    channel_config_set_transfer_data_size(&dm->config, DMA_SIZE_8);     // 8bit転送
    channel_config_set_read_increment(&dm->config, true);              // 読み込みアドレス増加
    channel_config_set_write_increment(&dm->config, false);            // 書き込みアドレス固定
    channel_config_set_dreq(&dm->config, DREQ_UART0_TX);              // UART0 TX DREQ（UART_PERIPHに対応）
    
    // 高優先度設定（UART FIFOを常に満杯に保つため）
    channel_config_set_high_priority(&dm->config, true);
    
    // 状態の初期化
    dm->transfer_in_progress = false;
    dm->bytes_transferred = 0;
    dm->transfer_errors = 0;
    
    // DMA完了割り込みハンドラの設定
    dma_channel_set_irq0_enabled(dm->dma_channel, true);
    
    // DMA IRQハンドラを設定（チャネル番号に応じて適切なIRQを選択）
    if (dm->dma_channel < 8) {
        irq_set_exclusive_handler(DMA_IRQ_0, bt_uart_dma_irq_handler);
        irq_set_enabled(DMA_IRQ_0, true);
    } else {
        // チャネル8-11はDMA_IRQ_1を使用
        irq_set_exclusive_handler(DMA_IRQ_1, bt_uart_dma_irq_handler);
        irq_set_enabled(DMA_IRQ_1, true);
    }
    
    // UART FIFOの最適化
    configure_uart_fifo_for_dma();
    
    printf("DMA manager initialized: channel=%d, DREQ=UART0_TX\n", dm->dma_channel);
    
    return 0;
}

static int bt_uart_start_dma_transfer(void) {
    // 読み込みバッファからDMA転送用データを準備
    uint8_t *buffer;
    uint16_t size;
    
    if (bt_uart_double_buffer_prepare_dma_locked(&bt_to_uart_double_buffer, &buffer, &size) != 0) {
        return -1;  // 転送するデータなし
    }
    
    // DMAマネージャーが初期化されているかチェック
    if (!bt_to_uart_dma_manager.channel_claimed) {
        handle_dma_error("DMA manager not initialized");
        return -1;
    }
    
    // 既にDMA転送中の場合はエラー
    if (bt_to_uart_dma_manager.transfer_in_progress) {
        handle_dma_error("DMA transfer already in progress");
        return -1;
    }
    
    // バッファサイズの妥当性チェック
    if (size == 0 || size > DMA_BUFFER_SIZE) {
        handle_dma_error("Invalid DMA transfer size");
        return -1;
    }
    
    // DMA転送開始前に状態を更新（1バイト転送でも確実に状態管理）
    bt_to_uart_dma_manager.transfer_in_progress = true;
    bt_to_uart_double_buffer.dma_in_progress = true;
    
    // UART送信FIFOをフル活用するためのDMA設定
    dma_channel_config config = bt_to_uart_dma_manager.config;
    
    // DMA転送を開始（UART FIFOが自動的に満杯まで使用される）
    dma_channel_configure(
        bt_to_uart_dma_manager.dma_channel,
        &config,
        &uart_get_hw(UART_PERIPH)->dr,  // UART送信データレジスタ
        buffer,                    // 送信データ
        size,                      // 転送サイズ
        true                       // 即座に開始
    );
    
    // printf("DMA transfer started: %d bytes from %p to UART\n", size, (void*)buffer);
    return 0;  // 転送開始成功（完了は割り込みで処理）
}

// DMAエラーハンドラー
static void handle_dma_error(const char* error_msg) {
    g_stats.dma_errors++;
    bt_to_uart_dma_manager.transfer_errors++;
    
    // DMA状態をリセット
    bt_to_uart_dma_manager.transfer_in_progress = false;
    bt_to_uart_double_buffer.dma_in_progress = false;
    
    // printf("DMA Error: %s\n", error_msg);
    status_manager_log(LOG_LEVEL_ERROR, "DMA Error: %s", error_msg);
}

static void bt_uart_dma_complete_handler(void) {
    uint64_t start_time = time_us_64();
    
    // BT受信割り込み中の場合、critical_sectionで待機
    critical_section_enter_blocking(&bt_to_uart_double_buffer.lock);
    
    // DMA転送完了処理
    bt_to_uart_dma_manager.transfer_in_progress = false;
    bt_to_uart_double_buffer.dma_in_progress = false;
    
    // 転送されたバイト数を保存（read_sizeをクリアする前に）
    uint16_t transferred_bytes = bt_to_uart_double_buffer.read_size;
    
    // DMA統計更新
    g_stats.dma_transfers++;
    g_stats.dma_bytes_transferred += transferred_bytes;
    bt_to_uart_dma_manager.bytes_transferred += transferred_bytes;
    
    // 転送時間測定
    uint64_t end_time = time_us_64();
    uint32_t transfer_time = (uint32_t)(end_time - start_time);
    if (transfer_time > g_stats.max_dma_transfer_time_us) {
        g_stats.max_dma_transfer_time_us = transfer_time;
    }
    
    // 読み込みバッファをクリア
    bt_to_uart_double_buffer.read_size = 0;
    
    // 必ずスワップを実行（書き込みバッファにデータがある場合）
    if (bt_to_uart_double_buffer.write_pos > 0) {
        bt_uart_double_buffer_swap(&bt_to_uart_double_buffer);
        g_stats.buffer_swaps++;
        
        // スワップ後、新しいデータがあれば次のDMA転送を開始
        if (bt_to_uart_double_buffer.read_size > 0) {
            critical_section_exit(&bt_to_uart_double_buffer.lock);
            bt_uart_start_dma_transfer();
        } else {
            critical_section_exit(&bt_to_uart_double_buffer.lock);
        }
    } else {
        critical_section_exit(&bt_to_uart_double_buffer.lock);
    }
    
    // printf("DMA transfer completed: %d bytes, %d us\n", 
    //        bt_to_uart_dma_manager.bytes_transferred, transfer_time);
}

// 割り込み制御関数（時間測定付き）
static uint32_t disable_irq(void) {
    return save_and_disable_interrupts();
}

static void restore_irq(uint32_t state) {
    restore_interrupts(state);
}

// 割り込み禁止時間を測定する関数
static uint32_t disable_irq_with_timing(void) {
    return save_and_disable_interrupts();
}

static void restore_irq_with_timing(uint32_t state, uint64_t start_time) {
    restore_interrupts(state);
    
    uint64_t end_time = time_us_64();
    uint32_t disable_time = (uint32_t)(end_time - start_time);
    
    if (disable_time > g_stats.max_interrupt_disable_us) {
        g_stats.max_interrupt_disable_us = disable_time;
    }
}

// UARTエラーハンドラ（維持）
static void uart_error_handler(uart_error_type_t error_type, uint32_t error_count) {
    // エラー統計に反映
    g_stats.transfer_errors++;
    
    // エラー種別に応じた処理
    if (error_type & UART_ERROR_OVERRUN) {
        g_stats.buffer_overflow_errors++;
    }
    
    if (error_type & UART_ERROR_FRAMING) {
        // フレーミングエラー処理
    }
    
    if (error_type & UART_ERROR_PARITY) {
        // パリティエラー処理
    }
    
    if (error_type & UART_ERROR_BREAK) {
        // ブレーク検出処理
    }
    
    if (error_type & UART_ERROR_TIMEOUT) {
        // タイムアウト処理（正常動作）
    }
    
    // 重大なエラーの場合はLED状態を変更
    if (error_type & (UART_ERROR_OVERRUN | UART_ERROR_FRAMING | UART_ERROR_PARITY)) {
        status_manager_set_led_state(LED_STATE_ERROR);
    }
}

// Bluetoothからのデータ受信コールバック（ダブルバッファ版）
static void bt_data_received_handler(uint8_t *data, uint16_t length) {
    // LED状態をデータ転送中に変更
    status_manager_set_led_state(LED_STATE_DATA_TRANSFER);
    
    // critical_sectionにより、DMA完了割り込みをブロック
    critical_section_enter_blocking(&bt_to_uart_double_buffer.lock);
    
    // データをダブルバッファに書き込み
    int result = bt_uart_double_buffer_write_locked(&bt_to_uart_double_buffer, data, length);
    
    if (result > 0) {
        // 統計更新
        g_stats.bt_to_uart_bytes += result;
        g_stats.bt_to_uart_packets++;
        
        // DMA転送中でない場合は即座にスワップして転送開始
        if (!bt_to_uart_double_buffer.dma_in_progress) {
            // readバッファが空の場合はスワップ
            if (bt_to_uart_double_buffer.read_size == 0) {
                bt_uart_double_buffer_swap(&bt_to_uart_double_buffer);
                g_stats.buffer_swaps++;
            }
            
            // スワップ後、readバッファにデータがあればDMA転送開始
            if (bt_to_uart_double_buffer.read_size > 0) {
                critical_section_exit(&bt_to_uart_double_buffer.lock);
                bt_uart_start_dma_transfer();
            } else {
                critical_section_exit(&bt_to_uart_double_buffer.lock);
            }
        } else {
            // DMA転送中の場合は書き込みのみ（完了時に自動スワップ）
            critical_section_exit(&bt_to_uart_double_buffer.lock);
        }
    } else {
        // バッファオーバーフロー
        g_stats.buffer_overflow_errors++;
        critical_section_exit(&bt_to_uart_double_buffer.lock);
    }
}

// UARTからのデータ受信コールバック（リングバッファ版）
static void uart_data_received_handler(uint8_t *data, uint16_t length) {
    // LED状態をデータ転送中に変更
    status_manager_set_led_state(LED_STATE_DATA_TRANSFER);
    
    uint32_t irq_state = disable_irq();
    
    uint16_t written = ring_buffer_write(&uart_to_bt_buffer, data, length);
    if (written < length) {
        g_stats.buffer_overflow_errors++;
    }
    
    restore_irq(irq_state);
}

static void bt_data_send_handler(void) {
    uint32_t irq_state = disable_irq();
    
    uint16_t available = ring_buffer_available_data(&uart_to_bt_buffer);
    if (available > 0) {
        uint8_t temp_buffer[BT_MAX_BATCH_SIZE];
        uint16_t batch_size = (available < BT_MAX_BATCH_SIZE) ? available : BT_MAX_BATCH_SIZE;
        
        // peek: 読み出し位置は更新しない
        uint16_t read_size = ring_buffer_peek(&uart_to_bt_buffer, temp_buffer, batch_size);

        if (bt_spp_get_connection_state() == BT_STATE_CONNECTED) {

            int sent = bt_spp_send_data(temp_buffer, read_size);
            
            if (sent > 0) {
                // 送信成功時のみバッファを更新
                ring_buffer_consume(&uart_to_bt_buffer, sent);
                
                // 統計更新
                g_stats.uart_to_bt_bytes += sent;
                g_stats.uart_to_bt_packets++;
            } else {
                // 送信失敗時は次回再トライ
                g_stats.transfer_errors++;
            }
        } else {
            // 接続されていない場合はバッファをクリア
            ring_buffer_clear(&uart_to_bt_buffer);
            g_stats.connection_errors++;
        }
    }

    available = ring_buffer_available_data(&uart_to_bt_buffer);
    //まだ残っていたら再発行
    if (available > 0) {
        bt_spp_request_can_send_now();
    }
    restore_irq(irq_state);
}

// Bluetooth接続状態変更コールバック
static void bt_connection_state_changed_handler(bt_connection_state_t state) {
    switch (state) {
        case BT_STATE_DISCONNECTED:
            status_manager_set_led_state(LED_STATE_PAIRING_WAIT);
            status_manager_log(LOG_LEVEL_INFO, "Bluetooth disconnected");
            break;
        case BT_STATE_PAIRING:
            status_manager_set_led_state(LED_STATE_PAIRING_WAIT);
            status_manager_log(LOG_LEVEL_INFO, "Bluetooth pairing");
            break;
        case BT_STATE_CONNECTED:
            status_manager_set_led_state(LED_STATE_CONNECTED);
            status_manager_log(LOG_LEVEL_INFO, "Bluetooth connected");
            break;
    }
}

int data_transfer_init(void) {
    if (engine_initialized) {
        status_manager_log(LOG_LEVEL_WARN, "Data transfer engine already initialized");
        return 0;
    }
    
    // 統計初期化
    memset(&g_stats, 0, sizeof(g_stats));
    
    // ダブルバッファ初期化
    if (bt_uart_double_buffer_init(&bt_to_uart_double_buffer) != 0) {
        status_manager_log(LOG_LEVEL_ERROR, "Failed to initialize double buffer");
        return -1;
    }
    
    // DMAマネージャー初期化
    if (bt_uart_dma_manager_init(&bt_to_uart_dma_manager) != 0) {
        status_manager_log(LOG_LEVEL_ERROR, "Failed to initialize DMA manager");
        return -1;
    }
    
    // 既存のリングバッファ初期化（UART→BT送信用は維持）
    ring_buffer_init(&uart_to_bt_buffer);
    // BT→UART用リングバッファは削除（ダブルバッファに置き換え）
    
    // フラグ初期化（DMA版では不要）
    // uart_tx_in_progress = false;  // 削除済み
    
    // Bluetoothコールバック設定（後で更新されるbt_data_received_handlerを使用）
    bt_spp_set_callbacks(bt_data_received_handler, bt_data_send_handler, bt_connection_state_changed_handler);
    
    // UARTコールバック設定
    uart_bridge_set_callback(uart_data_received_handler);
    uart_bridge_set_error_callback(uart_error_handler);
    // UART送信完了コールバックは削除（DMAを使用するため）
    
    engine_initialized = true;
    
    status_manager_log(LOG_LEVEL_INFO, "Data transfer engine initialized with DMA and double buffer");
    return 0;
}

// UART→BT送信処理
static void process_uart_to_bt_buffer(void) {
    
    uint32_t irq_state = disable_irq();
    
    uint16_t available = ring_buffer_available_data(&uart_to_bt_buffer);
    //送れるデータがあったら要求発行
    if (available > 0) {
        bt_spp_request_can_send_now();
    }
    restore_irq(irq_state);
}

void data_transfer_process(void) {
    if (!engine_initialized) {
        return;
    }
    // UART→BT送信処理
    process_uart_to_bt_buffer();
}

transfer_stats_t* data_transfer_get_stats(void) {
    return &g_stats;
}


// 統計をリセット（DMA版）
void data_transfer_reset_stats(void) {
    memset(&g_stats, 0, sizeof(g_stats));
    
    // UART→BT用リングバッファ初期化（維持）
    ring_buffer_clear(&uart_to_bt_buffer);
    
    // ダブルバッファ状態のリセット
    critical_section_enter_blocking(&bt_to_uart_double_buffer.lock);
    bt_to_uart_double_buffer.write_pos = 0;
    bt_to_uart_double_buffer.read_size = 0;
    // DMA転送中フラグはリセットしない（安全のため）
    critical_section_exit(&bt_to_uart_double_buffer.lock);
    
    // DMAマネージャー統計のリセット
    bt_to_uart_dma_manager.bytes_transferred = 0;
    bt_to_uart_dma_manager.transfer_errors = 0;
    
    status_manager_log(LOG_LEVEL_INFO, "Data transfer statistics and buffers reset (DMA version)");
}

// 統計を出力（DMA版）
void data_transfer_print_stats(void) {
    status_manager_log(LOG_LEVEL_INFO, "=== Data Transfer Statistics (DMA) ===");
    status_manager_log(LOG_LEVEL_INFO, "BT->UART bytes: %d", g_stats.bt_to_uart_bytes);
    status_manager_log(LOG_LEVEL_INFO, "BT->UART packets: %d", g_stats.bt_to_uart_packets);
    status_manager_log(LOG_LEVEL_INFO, "UART->BT bytes: %d", g_stats.uart_to_bt_bytes);
    status_manager_log(LOG_LEVEL_INFO, "UART->BT packets: %d", g_stats.uart_to_bt_packets);
    status_manager_log(LOG_LEVEL_INFO, "Transfer errors: %d", g_stats.transfer_errors);
    status_manager_log(LOG_LEVEL_INFO, "Buffer overflow errors: %d", g_stats.buffer_overflow_errors);
    status_manager_log(LOG_LEVEL_INFO, "Connection errors: %d", g_stats.connection_errors);
    
    // DMA統計情報
    status_manager_log(LOG_LEVEL_INFO, "--- DMA Statistics ---");
    status_manager_log(LOG_LEVEL_INFO, "DMA transfers: %d", g_stats.dma_transfers);
    status_manager_log(LOG_LEVEL_INFO, "DMA bytes transferred: %d", g_stats.dma_bytes_transferred);
    status_manager_log(LOG_LEVEL_INFO, "DMA errors: %d", g_stats.dma_errors);
    status_manager_log(LOG_LEVEL_INFO, "Max DMA transfer time: %d us", g_stats.max_dma_transfer_time_us);
    status_manager_log(LOG_LEVEL_INFO, "Buffer swaps: %d", g_stats.buffer_swaps);
    status_manager_log(LOG_LEVEL_INFO, "Bus conflicts: %d", g_stats.bus_conflicts);
    
    // パフォーマンス分析
    if (g_stats.bt_to_uart_packets > 0) {
        uint32_t avg_bt_packet_size = g_stats.bt_to_uart_bytes / g_stats.bt_to_uart_packets;
        status_manager_log(LOG_LEVEL_INFO, "Avg BT->UART packet size: %d bytes", avg_bt_packet_size);
    }
    
    if (g_stats.uart_to_bt_packets > 0) {
        uint32_t avg_uart_packet_size = g_stats.uart_to_bt_bytes / g_stats.uart_to_bt_packets;
        status_manager_log(LOG_LEVEL_INFO, "Avg UART->BT packet size: %d bytes", avg_uart_packet_size);
    }
    
    if (g_stats.dma_transfers > 0) {
        uint32_t avg_dma_size = g_stats.dma_bytes_transferred / g_stats.dma_transfers;
        status_manager_log(LOG_LEVEL_INFO, "Avg DMA transfer size: %d bytes", avg_dma_size);
    }
    
    // ダブルバッファ状態情報
    status_manager_log(LOG_LEVEL_INFO, "--- Buffer Status ---");
    status_manager_log(LOG_LEVEL_INFO, "UART->BT ring buffer: %d/%d bytes (overflow: %s)", 
                      uart_to_bt_buffer.count, RING_BUFFER_SIZE, 
                      uart_to_bt_buffer.overflow ? "Yes" : "No");
    status_manager_log(LOG_LEVEL_INFO, "Double buffer write pos: %d", bt_to_uart_double_buffer.write_pos);
    status_manager_log(LOG_LEVEL_INFO, "Double buffer read size: %d", bt_to_uart_double_buffer.read_size);
    status_manager_log(LOG_LEVEL_INFO, "DMA in progress: %s", bt_to_uart_double_buffer.dma_in_progress ? "Yes" : "No");
    
    // DMAマネージャー状態
    status_manager_log(LOG_LEVEL_INFO, "--- DMA Manager Status ---");
    status_manager_log(LOG_LEVEL_INFO, "DMA channel: %d", bt_to_uart_dma_manager.dma_channel);
    status_manager_log(LOG_LEVEL_INFO, "Channel claimed: %s", bt_to_uart_dma_manager.channel_claimed ? "Yes" : "No");
    status_manager_log(LOG_LEVEL_INFO, "Transfer in progress: %s", bt_to_uart_dma_manager.transfer_in_progress ? "Yes" : "No");

    status_manager_log(LOG_LEVEL_INFO, "===============================");
}
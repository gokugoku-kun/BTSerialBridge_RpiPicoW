#ifndef BT_SPP_HANDLER_H
#define BT_SPP_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

// Bluetooth接続状態
typedef enum {
    BT_STATE_DISCONNECTED,
    BT_STATE_PAIRING,
    BT_STATE_CONNECTED
} bt_connection_state_t;

// コールバック関数型定義
typedef void (*bt_data_received_callback_t)(uint8_t *data, uint16_t length);
typedef void (*bt_can_send_callback_t)(void);
typedef void (*bt_connection_state_changed_callback_t)(bt_connection_state_t state);

// 公開関数
int bt_spp_init(void);
int bt_spp_send_data(uint8_t *data, uint16_t length);
bt_connection_state_t bt_spp_get_connection_state(void);
void bt_spp_set_callbacks(bt_data_received_callback_t data_callback, 
                          bt_can_send_callback_t send_callback,
                         bt_connection_state_changed_callback_t state_callback);
void bt_spp_disconnect(void);
void bt_spp_get_connection_info(void);
void bt_spp_set_auto_reconnect(bool enabled);
void bt_spp_process_auto_reconnect(void);
void bt_spp_reset_reconnect_stats(void);
void bt_spp_request_can_send_now(void);

#endif // BT_SPP_HANDLER_H
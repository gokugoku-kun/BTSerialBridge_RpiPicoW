#include "bt_spp_handler.h"
#include "btstack.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "debug_gpio.h"
#define RFCOMM_SERVER_CHANNEL 1

static uint16_t rfcomm_channel_id = 0;
static uint8_t spp_service_buffer[150];
static btstack_packet_callback_registration_t hci_event_callback_registration;
static bt_connection_state_t current_state = BT_STATE_DISCONNECTED;
static bool auto_reconnect_enabled = true;
static uint32_t last_disconnect_time = 0;
static uint32_t reconnect_attempts = 0;
static bd_addr_t last_connected_device = {0};

// コールバック関数ポインタ
static bt_data_received_callback_t g_data_callback = NULL;
static bt_connection_state_changed_callback_t g_state_callback = NULL;
static bt_can_send_callback_t g_can_send_callback = NULL;

// 接続状態を更新し、コールバックを呼び出す
static void update_connection_state(bt_connection_state_t new_state) {
    if (current_state != new_state) {
        current_state = new_state;
        if (g_state_callback) {
            g_state_callback(new_state);
        }
    }
}

// SPPパケットハンドラ
static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    
    bd_addr_t event_addr;
    uint8_t rfcomm_channel_nr;
    uint16_t mtu;
    debug_gpio_toggle(PIN_DEBUG_2);
    switch (packet_type) {
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)) {
                case HCI_EVENT_PIN_CODE_REQUEST:
                    printf("Pin code request - using '0000'\n");
                    hci_event_pin_code_request_get_bd_addr(packet, event_addr);
                    gap_pin_code_response(event_addr, "0000");
                    break;
                    
                case HCI_EVENT_USER_CONFIRMATION_REQUEST:
                    printf("SSP User Confirmation Request with numeric value '%06"PRIu32"'\n", 
                           little_endian_read_32(packet, 8));
                    printf("SSP User Confirmation Auto accept\n");
                    update_connection_state(BT_STATE_PAIRING);
                    break;
                    
                case RFCOMM_EVENT_INCOMING_CONNECTION:
                    rfcomm_event_incoming_connection_get_bd_addr(packet, event_addr);
                    rfcomm_channel_nr = rfcomm_event_incoming_connection_get_server_channel(packet);
                    rfcomm_channel_id = rfcomm_event_incoming_connection_get_rfcomm_cid(packet);
                    
                    // 単一接続制限：既に接続がある場合は拒否
                    if (current_state == BT_STATE_CONNECTED) {
                        printf("Connection rejected: already connected to another device\n");
                        rfcomm_decline_connection(rfcomm_channel_id);
                        rfcomm_channel_id = 0;
                    } else {
                        printf("RFCOMM channel %u requested for %s\n", rfcomm_channel_nr, bd_addr_to_str(event_addr));
                        memcpy(last_connected_device, event_addr, 6);
                        rfcomm_accept_connection(rfcomm_channel_id);
                    }
                    break;
                    
                case RFCOMM_EVENT_CHANNEL_OPENED:
                    if (rfcomm_event_channel_opened_get_status(packet)) {
                        printf("RFCOMM channel open failed, status %u\n", 
                               rfcomm_event_channel_opened_get_status(packet));
                        update_connection_state(BT_STATE_DISCONNECTED);
                        rfcomm_channel_id = 0;
                    } else {
                        rfcomm_channel_id = rfcomm_event_channel_opened_get_rfcomm_cid(packet);
                        mtu = rfcomm_event_channel_opened_get_max_frame_size(packet);
                        printf("RFCOMM channel open succeeded. New RFCOMM Channel ID %u, max frame size %u\n", 
                               rfcomm_channel_id, mtu);
                        update_connection_state(BT_STATE_CONNECTED);
                        reconnect_attempts = 0; // 接続成功時にリセット
                    }
                    break;
                    
                case RFCOMM_EVENT_CAN_SEND_NOW:
                    // 送信可能イベント - コールバック呼び出し
                    if (g_can_send_callback) {
                        g_can_send_callback();
                    }
                    break;
                    
                case RFCOMM_EVENT_CHANNEL_CLOSED:
                    printf("RFCOMM channel closed\n");
                    rfcomm_channel_id = 0;
                    last_disconnect_time = to_ms_since_boot(get_absolute_time());
                    update_connection_state(BT_STATE_DISCONNECTED);
                    
                    // 自動再接続の準備
                    if (auto_reconnect_enabled) {
                        printf("Auto-reconnect enabled, will attempt reconnection\n");
                    }
                    break;
                    
                default:
                    break;
            }
            break;
            
        case RFCOMM_DATA_PACKET:
            // 受信データをコールバックに渡す
            if (g_data_callback) {
                g_data_callback(packet, size);
            }
            // printf("RCV: %d bytes\n", size);
            break;
            
        default:
            break;
    }
    debug_gpio_toggle(PIN_DEBUG_2);
}

// SPPサービスセットアップ
static void spp_service_setup(void) {
    // HCIイベントハンドラ登録
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    
    l2cap_init();
    
#ifdef ENABLE_BLE
    // LE Security Manager初期化
    sm_init();
#endif
    
    rfcomm_init();
    rfcomm_register_service(packet_handler, RFCOMM_SERVER_CHANNEL, 0xffff);
    
    // SDP初期化とレコード作成
    sdp_init();
    memset(spp_service_buffer, 0, sizeof(spp_service_buffer));
    spp_create_sdp_record(spp_service_buffer, 0x10001, RFCOMM_SERVER_CHANNEL, "BT Serial Bridge");
    sdp_register_service(spp_service_buffer);
    printf("SDP service record size: %u\n", de_get_len(spp_service_buffer));
}

int bt_spp_init(void) {
    // SPPサービスセットアップ
    spp_service_setup();
    
    // Bluetooth設定
    gap_discoverable_control(1);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_DISPLAY_YES_NO);
    gap_set_local_name("BT Serial Bridge");
    
    // Bluetooth有効化
    hci_power_control(HCI_POWER_ON);
    
    printf("Bluetooth SPP Handler initialized\n");
    return 0;
}

int bt_spp_send_data(uint8_t *data, uint16_t length) {
    if (!data || length == 0) {
        return -1;
    }
    
    if (rfcomm_channel_id == 0 || current_state != BT_STATE_CONNECTED) {
        printf("BT not connected, cannot send data\n");
        return -1;
    }
    
    // Bluetoothバッファの状態をチェック
    if (!rfcomm_can_send_packet_now(rfcomm_channel_id)) {
        printf("BT buffer full, cannot send data\n");
        return -1;
    }
    
    // データ送信
    int result = rfcomm_send(rfcomm_channel_id, data, length);
    if (result == 0) {
        //printf("BT sent %d bytes\n", length);
        return length;
    } else {
        printf("BT send failed: %d\n", result);
        return -1;
    }
}

bt_connection_state_t bt_spp_get_connection_state(void) {
    return current_state;
}

void bt_spp_set_callbacks(bt_data_received_callback_t data_callback,
                         bt_can_send_callback_t send_callback,
                         bt_connection_state_changed_callback_t state_callback) {
    g_data_callback = data_callback;
    g_can_send_callback = send_callback;
    g_state_callback = state_callback;
}

// 接続を強制切断
void bt_spp_disconnect(void) {
    if (rfcomm_channel_id != 0) {
        rfcomm_disconnect(rfcomm_channel_id);
        printf("BT disconnect requested\n");
    }
}

// 接続統計を取得
void bt_spp_get_connection_info(void) {
    printf("=== Bluetooth Connection Info ===\n");
    printf("State: ");
    switch (current_state) {
        case BT_STATE_DISCONNECTED:
            printf("DISCONNECTED\n");
            break;
        case BT_STATE_PAIRING:
            printf("PAIRING\n");
            break;
        case BT_STATE_CONNECTED:
            printf("CONNECTED\n");
            break;
    }
    printf("RFCOMM Channel ID: %u\n", rfcomm_channel_id);
    printf("Auto-reconnect: %s\n", auto_reconnect_enabled ? "Enabled" : "Disabled");
    printf("Reconnect attempts: %d\n", reconnect_attempts);
    if (last_disconnect_time > 0) {
        uint32_t time_since_disconnect = to_ms_since_boot(get_absolute_time()) - last_disconnect_time;
        printf("Time since last disconnect: %d ms\n", time_since_disconnect);
    }
    printf("Last connected device: %s\n", bd_addr_to_str(last_connected_device));
    printf("================================\n");
}

// 自動再接続の有効/無効設定
void bt_spp_set_auto_reconnect(bool enabled) {
    auto_reconnect_enabled = enabled;
    printf("Auto-reconnect %s\n", enabled ? "enabled" : "disabled");
}

// 自動再接続処理（定期的に呼び出す）
void bt_spp_process_auto_reconnect(void) {
    if (!auto_reconnect_enabled || current_state != BT_STATE_DISCONNECTED) {
        return;
    }
    
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // 切断から5秒後に再接続を試行
    if (last_disconnect_time > 0 && (current_time - last_disconnect_time) > 5000) {
        if (reconnect_attempts < 5) { // 最大5回まで試行
            printf("Attempting auto-reconnect (attempt %d/5)\n", reconnect_attempts + 1);
            
            // 再接続試行（discoverable状態を再設定）
            gap_discoverable_control(1);
            
            reconnect_attempts++;
            last_disconnect_time = current_time; // 次の試行まで5秒待つ
        } else {
            printf("Max reconnect attempts reached, giving up\n");
            auto_reconnect_enabled = false;
        }
    }
}

// 再接続統計をリセット
void bt_spp_reset_reconnect_stats(void) {
    reconnect_attempts = 0;
    last_disconnect_time = 0;
    printf("Reconnect statistics reset\n");
}

// UART受信時のrfcomm_request_can_send_now_event呼び出し
void bt_spp_request_can_send_now(void) {
    if (rfcomm_channel_id != 0 && current_state == BT_STATE_CONNECTED) {
        rfcomm_request_can_send_now_event(rfcomm_channel_id);
    }
}

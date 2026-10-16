#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool connected, ready, pairing_open, confirm, numeric, audio_subscribed;
    uint32_t number, sync_request, accepted, epoch, confirmation_id;
    char device_id[18], host_name[33], prompt[96], error[64];
} app_ble_status_t;
bool app_ble_init(void);
app_ble_status_t app_ble_status(void);
void app_ble_open_pairing(void);
void app_ble_confirm(bool accept, uint32_t displayed_confirmation_id);
void app_ble_forget(void);
void app_ble_request_sync(void);
bool app_ble_audio_send(const uint8_t *data, size_t size);

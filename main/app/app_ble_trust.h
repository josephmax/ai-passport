#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct { uint8_t type, val[6]; } app_ble_peer_t;
typedef struct { char id[37], name[33]; app_ble_peer_t peer; } app_ble_host_t;
typedef struct { uint32_t version; char active[37]; app_ble_host_t hosts[4]; } app_ble_bindings_t;
typedef enum { APP_BLE_TRUST_REJECT, APP_BLE_TRUST_RESUME, APP_BLE_TRUST_CONFIRM } app_ble_trust_t;
bool app_ble_bindings_valid(const app_ble_bindings_t *bindings);
app_ble_trust_t app_ble_trust(const app_ble_bindings_t *bindings, const char *host,
                              const app_ble_peer_t *peer, bool pairing_open);

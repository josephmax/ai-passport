#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_BLE_SNAPSHOT_MAX 4096
#define APP_BLE_AUDIO_SAMPLES 320
#define APP_BLE_AUDIO_BYTES 172
/* One ordered transaction. 1=begin(u16 size,u32 CRC32), 2=data(u16 offset,
 * bytes), 3=commit. Every integer is little endian; CRC is IEEE CRC32. */
typedef struct {
    char json[APP_BLE_SNAPSHOT_MAX];
    uint16_t expected, used;
    uint32_t crc;
    bool receiving;
} app_ble_rx_t;
typedef enum { APP_BLE_RX_ERROR=-1, APP_BLE_RX_MORE=0, APP_BLE_RX_COMPLETE=1 } app_ble_rx_result_t;
uint32_t app_ble_crc32(const void *data, size_t size);
app_ble_rx_result_t app_ble_rx_feed(app_ble_rx_t *rx, const uint8_t *data, size_t size);
/* Independent IMA ADPCM blocks: version(1), stream(u32), sequence(u32),
 * initial predictor(i16), index(u8), 319 nibbles (low nibble first). */
void app_ble_audio_encode(const int16_t pcm[APP_BLE_AUDIO_SAMPLES], uint32_t stream,
                          uint32_t sequence, uint8_t out[APP_BLE_AUDIO_BYTES]);
bool app_ble_audio_decode(const uint8_t *data, size_t size, int16_t pcm[APP_BLE_AUDIO_SAMPLES]);
bool app_ble_host_id_valid(const char *id);

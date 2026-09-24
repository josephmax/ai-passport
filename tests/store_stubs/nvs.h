#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
typedef int nvs_handle_t;
#define NVS_READWRITE 1
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1110
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_get_str(nvs_handle_t, const char *, char *, size_t *);
esp_err_t nvs_set_str(nvs_handle_t, const char *, const char *);
esp_err_t nvs_commit(nvs_handle_t);
#define NVS_SCALAR(type, name) \
    esp_err_t nvs_get_##name(nvs_handle_t, const char *, type *); \
    esp_err_t nvs_set_##name(nvs_handle_t, const char *, type);
NVS_SCALAR(uint8_t, u8)
NVS_SCALAR(int32_t, i32)
NVS_SCALAR(int64_t, i64)

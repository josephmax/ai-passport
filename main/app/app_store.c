// main/app/app_store.c —— NVS 持久化实现。
// 读失败/缺键一律回默认值,绝不因存储问题阻塞启动。
#include "app_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "store";
static const char *NS = "pendant";

static nvs_handle_t s_nvs;

#define KEY_SETTINGS "set"
#define KEY_SNAPSHOT "snap"
#define KEY_FOCUS    "focus"
#define KEY_XP       "xp"
#define KEY_NET      "net"
#define KEY_BUNDLE   "bundle"
#define KEY_LASTSYNC "lsync"

void app_store_init(void) {
    if (s_nvs) return;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 分区布局变更(如基线 → 挂坠)后的首次启动会走到这里:擦后重试。
        ESP_LOGW(TAG, "NVS 需要重新格式化: %s", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s(降级为内存态)", esp_err_to_name(err));
        return;
    }
    err = nvs_open(NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 打开失败: %s(降级为内存态)", esp_err_to_name(err));
    }
}

static bool read_blob(const char *key, void *out, size_t len) {
    if (!s_nvs) return false;
    size_t got = 0;
    return nvs_get_blob(s_nvs, key, out, &got) == ESP_OK && got == len;
}

static void write_blob(const char *key, const void *data, size_t len) {
    if (!s_nvs || nvs_set_blob(s_nvs, key, data, len) != ESP_OK) return;
    (void)nvs_commit(s_nvs);
}

app_settings_t app_store_settings(void) {
    app_settings_t s = {
        .brightness = 3,
        .volume = 3,
        .auto_off_s = 30,
        .rest_start_min = 23 * 60,
        .rest_end_min = 8 * 60,
    };
    read_blob(KEY_SETTINGS, &s, sizeof(s));
    if (s.brightness < 1) s.brightness = 1;
    if (s.brightness > 5) s.brightness = 5;
    if (s.volume > 5) s.volume = 5;
    if (s.auto_off_s == 0) s.auto_off_s = 30;
    return s;
}

void app_store_save_settings(const app_settings_t *settings) {
    write_blob(KEY_SETTINGS, settings, sizeof(*settings));
}

bool app_store_snapshot_json(char *buf, size_t cap) {
    if (!s_nvs) return false;
    size_t got = cap;
    if (nvs_get_str(s_nvs, KEY_SNAPSHOT, buf, &got) != ESP_OK) return false;
    return true;
}

bool app_store_save_snapshot_json(const char *json) {
    if (!s_nvs || strlen(json) + 1 > 4096) return false;   // 快照上限 4KB
    if (nvs_set_str(s_nvs, KEY_SNAPSHOT, json) != ESP_OK) return false;
    return nvs_commit(s_nvs) == ESP_OK;
}

app_focus_state_t app_store_focus(void) {
    app_focus_state_t st;
    if (read_blob(KEY_FOCUS, &st, sizeof(st))) {
        // 合理性防御:结束时刻超出 [now, now + 5 单元] 视为脏数据丢弃。
        return st;
    }
    app_focus_reset(&st);
    return st;
}

void app_store_save_focus(const app_focus_state_t *focus) {
    write_blob(KEY_FOCUS, focus, sizeof(*focus));
}

app_xp_store_t app_store_xp(void) {
    app_xp_store_t st;
    if (read_blob(KEY_XP, &st, sizeof(st))) return st;
    app_xp_store_reset(&st);
    return st;
}

void app_store_save_xp(const app_xp_store_t *xp) {
    write_blob(KEY_XP, xp, sizeof(*xp));
}

bool app_store_net(app_net_cfg_t *out) {
    memset(out, 0, sizeof(*out));
    return read_blob(KEY_NET, out, sizeof(*out)) && out->ssid[0] != '\0';
}

bool app_store_save_net(const app_net_cfg_t *net) {
    if (!s_nvs || nvs_set_blob(s_nvs, KEY_NET, net, sizeof(*net)) != ESP_OK) {
        return false;
    }
    return nvs_commit(s_nvs) == ESP_OK;
}

int app_store_bundle_version(void) {
    int32_t v = -1;
    if (!s_nvs || nvs_get_i32(s_nvs, KEY_BUNDLE, &v) != ESP_OK) return -1;
    return (int)v;
}

void app_store_save_bundle_version(int v) {
    if (!s_nvs || nvs_set_i32(s_nvs, KEY_BUNDLE, (int32_t)v) != ESP_OK) return;
    (void)nvs_commit(s_nvs);
}

int64_t app_store_last_sync_ms(void) {
    int64_t v = 0;
    if (!s_nvs || nvs_get_i64(s_nvs, KEY_LASTSYNC, &v) != ESP_OK) return 0;
    return v;
}

void app_store_save_last_sync_ms(int64_t ms) {
    if (!s_nvs || nvs_set_i64(s_nvs, KEY_LASTSYNC, ms) != ESP_OK) return;
    (void)nvs_commit(s_nvs);
}

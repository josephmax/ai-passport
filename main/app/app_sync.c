// main/app/app_sync.c —— 同步任务实现(esp_http_client 流式,绝不整包进 RAM)。
#include "app_sync.h"

#include "app_net.h"
#include "app_debug.h"
#include "app_runtime.h"
#include "app_service.h"
#include "app_snapshot.h"
#include "app_store.h"
#include "app_time.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys/time.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "sync";
#define SYNC_PERIOD_MS (60LL * 60 * 1000)
#define HTTP_TIMEOUT_MS 12000
#define SNAPSHOT_MAX 4096
#define DL_CHUNK 4096
#define BUNDLE_FILES_MAX 16
#define BUNDLE_NAME_MAX 64

static TaskHandle_t s_task;
static volatile bool s_busy;

typedef struct {
    esp_http_client_handle_t h;
    uint8_t buf[DL_CHUNK];
    size_t len, pos;
} chunk_reader_t;

static bool cr_fill(chunk_reader_t *r) {
    if (r->pos < r->len) return true;
    int n = esp_http_client_read(r->h, (char *)r->buf, sizeof(r->buf));
    if (n <= 0) return false;
    r->len = (size_t)n;
    r->pos = 0;
    return true;
}

static bool cr_read(chunk_reader_t *r, void *out, size_t n) {
    uint8_t *o = out;
    while (n > 0) {
        if (!cr_fill(r)) return false;
        size_t take = r->len - r->pos;
        if (take > n) take = n;
        memcpy(o, r->buf + r->pos, take);
        r->pos += take;
        o += take;
        n -= take;
    }
    return true;
}

static bool cr_u16(chunk_reader_t *r, uint16_t *v) {
    uint8_t b[2];
    if (!cr_read(r, b, 2)) return false;
    *v = (uint16_t)(b[0] | (b[1] << 8));
    return true;
}

static bool cr_u32(chunk_reader_t *r, uint32_t *v) {
    uint8_t b[4];
    if (!cr_read(r, b, 4)) return false;
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
         ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

static bool http_read_all(const char *url, const char *token,
                          char *body, size_t cap, int *status) {
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return false;
    if (token) esp_http_client_set_header(h, "X-Device-Token", token);
    esp_err_t err = esp_http_client_open(h, 0);
    bool ok = false;
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(h);
        int code = esp_http_client_get_status_code(h);
        if (status) *status = code;
        if (code == 200) {
            size_t got = 0;
            while (got + 1 < cap) {
                int n = esp_http_client_read(h, body + got, cap - 1 - got);
                if (n <= 0) break;
                got += (size_t)n;
            }
            body[got] = '\0';
            ok = got > 0;
        }
    }
    esp_http_client_cleanup(h);
    return ok;
}

// 配对:POST /api/pair {"code","deviceName"} → {"token"}(规格 §4 草案)。
bool app_sync_pair(const char *base, const char *code) {
    char url[160], body[96], resp[512];
    snprintf(url, sizeof(url), "%s/api/pair", base);
    snprintf(body, sizeof(body),
             "{\"code\":\"%s\",\"deviceName\":\"AI Passport\"}", code);

    esp_http_client_config_t cfg = {
        .url = url, .timeout_ms = HTTP_TIMEOUT_MS, .keep_alive_enable = false,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return false;
    esp_http_client_set_method(h, HTTP_METHOD_POST);
    esp_http_client_set_header(h, "Content-Type", "application/json");
    esp_http_client_set_post_field(h, body, (int)strlen(body));
    bool http_ok = false;
    int status = 0;
    size_t got = 0;
    if (esp_http_client_open(h, strlen(body)) == ESP_OK) {
        if (esp_http_client_write(h, body, (int)strlen(body)) >= 0) {
            (void)esp_http_client_fetch_headers(h);
            status = esp_http_client_get_status_code(h);
            if (status == 200) {
                while (got + 1 < sizeof(resp)) {
                    int n = esp_http_client_read(h, resp + got, sizeof(resp) - 1 - got);
                    if (n <= 0) break;
                    got += (size_t)n;
                }
                resp[got] = '\0';
                http_ok = got > 0;
            }
        }
    }
    esp_http_client_cleanup(h);
    if (!http_ok) {
        ESP_LOGW(TAG, "配对请求失败: HTTP %d", status);
        return false;
    }
    const char *rp = resp;
    const char *t = strstr(rp, "\"token\"");
    t = t ? strchr(t + 7, '"') : NULL;
    if (!t) return false;
    t++;
    const char *e = strchr(t, '"');
    if (!e || (size_t)(e - t) >= APP_DEV_TOKEN_MAX) return false;

    app_net_cfg_t net;
    app_store_net(&net);
    memcpy(net.device_token, t, (size_t)(e - t));
    net.device_token[e - t] = '\0';
    if (!app_store_save_net(&net)) return false;
    app_runtime_t *rt = app_runtime();
    strlcpy(rt->service_url, net.service_url, sizeof(rt->service_url));
    rt->paired = true;
    ESP_LOGI(TAG, "配对成功");
    return true;
}

// APB1 流式下载:全部文件先写 .part,最后统一 rename 原子切换;失败清理
// 残留,旧版本不受影响(规格 §8/ADR-0004)。
static bool download_bundle(const char *base, int version) {
    char url[160];
    snprintf(url, sizeof(url), "%s/assets/bundle_v%d.bin", base, version);
    esp_http_client_config_t cfg = {
        .url = url, .timeout_ms = HTTP_TIMEOUT_MS, .keep_alive_enable = false,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return false;

    static char names[BUNDLE_FILES_MAX][BUNDLE_NAME_MAX + 8];
    int file_count_total = 0;
    FILE *out = NULL;
    bool ok = false;

    if (esp_http_client_open(h, 0) != ESP_OK) goto out;
    (void)esp_http_client_fetch_headers(h);
    if (esp_http_client_get_status_code(h) != 200) goto out;

    {
        // 读取器内嵌 4KB 缓冲,放静态:同步任务栈 6KB 扛不住栈上 4KB。
        static chunk_reader_t r;
        r.h = h;
        r.len = 0;
        r.pos = 0;
        uint8_t magic[4];
        uint16_t count = 0;
        if (!cr_read(&r, magic, 4) || memcmp(magic, "APB1", 4) != 0) goto out;
        if (!cr_u16(&r, &count) || count == 0 || count > BUNDLE_FILES_MAX) goto out;
        file_count_total = count;

        for (int i = 0; i < count; i++) {
            uint16_t name_len = 0;
            uint32_t data_len = 0;
            char name[BUNDLE_NAME_MAX + 1];
            if (!cr_u16(&r, &name_len) || name_len == 0 ||
                name_len > BUNDLE_NAME_MAX) goto out;
            if (!cr_read(&r, name, name_len)) goto out;
            name[name_len] = '\0';
            if (!cr_u32(&r, &data_len)) goto out;

            char path[96];
            snprintf(path, sizeof(path), "/assets/bundle/%s.part", name);
            out = fopen(path, "wb");
            if (!out) goto out;
            snprintf(names[i], sizeof(names[i]), "%s", name);
            // 数据按块搬运,任何时刻 RAM 占用不超过 DL_CHUNK。
            while (data_len > 0) {
                if (!cr_fill(&r)) goto out;
                size_t take = r.len - r.pos;
                if (take > data_len) take = data_len;
                if (fwrite(r.buf + r.pos, 1, take, out) != take) goto out;
                r.pos += take;
                data_len -= take;
            }
            fclose(out);
            out = NULL;
        }
        // 全部落盘成功:统一切换。
        for (int i = 0; i < count; i++) {
            char tmp[96], dst[96];
            snprintf(tmp, sizeof(tmp), "/assets/bundle/%s.part", names[i]);
            snprintf(dst, sizeof(dst), "/assets/bundle/%s", names[i]);
            if (rename(tmp, dst) != 0) goto out;
        }
        ok = true;
    }

out:
    if (out) fclose(out);
    esp_http_client_cleanup(h);
    if (!ok) {
        for (int i = 0; i < file_count_total; i++) {
            if (!names[i][0]) break;
            char tmp[96];
            snprintf(tmp, sizeof(tmp), "/assets/bundle/%s.part", names[i]);
            unlink(tmp);
        }
    } else {
        app_store_save_bundle_version(version);
        ESP_LOGI(TAG, "素材包 v%d 安装完成", version);
    }
    return ok;
}

static void do_sync(void) {
    s_busy = true;
    app_runtime_t *rt = app_runtime();
    app_net_cfg_t net;
    // 早退也必须复位 s_busy:否则一次未配对/未联网的触发会让
    // app_sync_busy() 永久为 true。
    if (!app_store_net(&net) || !net.device_token[0]) {   // 未配对
        s_busy = false;
        return;
    }
    if (!app_net_connected()) {
        s_busy = false;
        return;
    }

    rt->sync_in_progress = true;
    rt->sync_result_valid = false;
    app_runtime_publish(APP_EVENT_SYNC_STATE);

    static char body[SNAPSHOT_MAX];
    char url[160];
    int status = 0;
    bool snapshot_ok = false;
    snprintf(url, sizeof(url), "%s/api/snapshot", net.service_url);
    if (http_read_all(url, net.device_token, body, sizeof(body), &status) &&
        status == 200) {
        static app_snapshot_t snap;
        if (app_snapshot_parse(body, strlen(body), &snap)) {
            snapshot_ok = true;
            if (strcmp(rt->badge.name, snap.badge_name) != 0 ||
                strcmp(rt->badge.role, snap.badge_role) != 0) {
                strlcpy(rt->badge.name, snap.badge_name, sizeof(rt->badge.name));
                strlcpy(rt->badge.role, snap.badge_role, sizeof(rt->badge.role));
                app_store_save_badge(&rt->badge);
            }
            // A cached snapshot may be minutes old. Only its response timestamp
            // represents current service time; keep generatedAt for freshness.
            int64_t server_now_ms = snap.served_at_ms ?
                snap.served_at_ms : snap.generated_at_ms;
            struct timeval tv;
            gettimeofday(&tv, NULL);
            int64_t local_now_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
            int64_t drift = local_now_ms - server_now_ms;
            // 低于 2020 的服务器时间(误配/垃圾响应)不采纳:把设备时钟
            // 拨回史前会让日界/作息全错,宁可等下一次同步。
            if (server_now_ms >= (int64_t)APP_TIME_PLAUSIBLE_S * 1000 &&
                (drift > 60000 || drift < -60000)) {
                struct timeval set = { .tv_sec = server_now_ms / 1000,
                                       .tv_usec = 0 };
                settimeofday(&set, NULL);
            }
            gettimeofday(&tv, NULL);
            rt->snap_received_at_ms =
                (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
            app_runtime_set_snapshot(&snap);
            rt->snap_valid = true;
            rt->weather_code = snap.weather_code;
            if (snap.sunrise_min > 0 && snap.sunset_min > snap.sunrise_min) {
                rt->sunrise_min = snap.sunrise_min;
                rt->sunset_min = snap.sunset_min;
            }
            app_store_save_snapshot_json(body);
            app_store_save_last_sync_ms(rt->snap_received_at_ms);
            app_runtime_publish(APP_EVENT_SNAPSHOT_UPDATED);

            if (snap.bundle_version > app_store_bundle_version()) {
                ESP_LOGI(TAG, "发现素材包 v%d(本地 v%d),下载",
                         snap.bundle_version, app_store_bundle_version());
                if (!download_bundle(net.service_url, snap.bundle_version)) {
                    ESP_LOGW(TAG, "素材包下载失败,下次同步重试");
                }
            }
        } else {
            ESP_LOGW(TAG, "快照解析失败");
        }
    } else if (status == 401) {
        ESP_LOGW(TAG, "令牌被拒(可能已吊销),进入待配对态");
        net.device_token[0] = '\0';
        app_store_save_net(&net);
        rt->paired = false;
        app_runtime_publish(APP_EVENT_WIFI_STATE);
    } else {
        ESP_LOGW(TAG, "快照拉取失败: HTTP %d", status);
    }

    rt->sync_in_progress = false;
    rt->sync_last_ok = snapshot_ok;
    rt->sync_result_valid = true;
    s_busy = false;
    app_runtime_publish(APP_EVENT_SYNC_STATE);
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)id; (void)data;
    if (s_task) xTaskNotifyGive(s_task);   // 联网即同步一次(规格 §8)
}

static void sync_task(void *arg) {
    (void)arg;
    // 首次配对(有凭证+配对码但无令牌)也在此触发:配网流程保存后重启进入。
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SYNC_PERIOD_MS));
        do_sync();
    }
}

bool app_sync_init(void) {
    if (s_task) return true;
    if (xTaskCreate(sync_task, "sync", 6144, NULL, 3, &s_task) != pdPASS) {
        return false;
    }
    app_debug_register("sync", s_task);
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               on_got_ip, NULL));
    return true;
}

void app_sync_request_now(void) {
    if (s_task) xTaskNotifyGive(s_task);
}

bool app_sync_busy(void) {
    return s_busy;
}

// main/app/app_assets.c —— LittleFS 素材包实现。
#include "app_assets.h"

#include "app_store.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "cJSON.h"

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "assets";
#define MOUNT "/assets"
#define PART_LABEL "assets"

// 固件内嵌默认包(EMBED_FILES 注入的符号)。
extern const uint8_t pendant_default_bundle_start[] asm("_binary_pendant_default_bundle_bin_start");
extern const uint8_t pendant_default_bundle_end[] asm("_binary_pendant_default_bundle_bin_end");

// ---- APB1 读取游标:包体要么在 flash 映射区,要么就是下载好的文件 ----
typedef struct {
    const uint8_t *p;
    const uint8_t *end;
} apb_reader_t;

static bool apb_read(apb_reader_t *r, void *out, size_t n) {
    if ((size_t)(r->end - r->p) < n) return false;
    memcpy(out, r->p, n);
    r->p += n;
    return true;
}

static bool apb_read_u16(apb_reader_t *r, uint16_t *out) {
    uint8_t b[2];
    if (!apb_read(r, b, 2)) return false;
    *out = (uint16_t)(b[0] | (b[1] << 8));
    return true;
}

static bool apb_read_u32(apb_reader_t *r, uint32_t *out) {
    uint8_t b[4];
    if (!apb_read(r, b, 4)) return false;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

// LittleFS 不自动创建父目录:安装/更新包前先把 bundle 目录建出来。
static void ensure_bundle_dir(void) {
    struct stat st = { 0 };
    if (stat(MOUNT "/bundle", &st) != 0) {
        mkdir(MOUNT "/bundle", 0775);
    }
}

static int install_embedded_bundle(void) {
    ensure_bundle_dir();
    apb_reader_t r = { pendant_default_bundle_start, pendant_default_bundle_end };
    uint16_t version = 0, count = 0;
    if (memcmp(r.p, "APB1", 4) != 0) return -1;
    r.p += 4;
    if (!apb_read_u16(&r, &version) || !apb_read_u16(&r, &count)) return -1;

    char path[96];
    int installed = 0;
    for (uint16_t i = 0; i < count; i++) {
        uint16_t name_len = 0;
        uint32_t data_len = 0;
        if (!apb_read_u16(&r, &name_len) || name_len > 64) break;
        char name[65];
        if (!apb_read(&r, name, name_len)) break;
        name[name_len] = '\0';
        if (!apb_read_u32(&r, &data_len)) break;

        snprintf(path, sizeof(path), MOUNT "/bundle/%s", name);
        FILE *f = fopen(path, "wb");
        if (!f) {
            ESP_LOGE(TAG, "写入失败: %s", path);
            r.p += data_len;   // 跳过继续(下一个文件也许能写)
            continue;
        }
        // 内嵌包在 flash 映射区,可一次性 fwrite;data_len 上限由打包方保证。
        if (fwrite(r.p, 1, data_len, f) != data_len) {
            ESP_LOGE(TAG, "写 %s 不完整", path);
            fclose(f);
            unlink(path);
        } else {
            fclose(f);
            installed++;
        }
        r.p += data_len;
        if (r.p > r.end) return -1;   // 损坏包:长度字段越过包尾,立即放弃
    }
    if (installed != count) return -1;
    ESP_LOGI(TAG, "默认素材包 v%u 安装完成(%d 文件)", version, installed);
    return version;
}

// 当前安装版本与固件内嵌版本一致时,素材直接从 flash 映射区读取:
// 地图条带 76.8K + 动作帧 48K 不再占用宝贵的系统堆(规格 §11 红线,
// Wi-Fi 需要约 60K 堆,首刷时正是这两块把 esp_wifi_init 挤成了 NO_MEM)。
// 下载的自定义包(P2)仍走 LittleFS 文件路径。
static uint16_t s_embedded_version = 0;
static bool s_use_embedded = false;

static const uint8_t *embedded_file(const char *name, size_t *len) {
    apb_reader_t r = { pendant_default_bundle_start, pendant_default_bundle_end };
    uint16_t version = 0, count = 0;
    if (memcmp(r.p, "APB1", 4) != 0) return NULL;
    r.p += 4;
    if (!apb_read_u16(&r, &version) || !apb_read_u16(&r, &count)) return NULL;
    for (uint16_t i = 0; i < count; i++) {
        uint16_t name_len = 0;
        uint32_t data_len = 0;
        if (!apb_read_u16(&r, &name_len) || name_len > 64) return NULL;
        char cur[65];
        if (!apb_read(&r, cur, name_len)) return NULL;
        cur[name_len] = '\0';
        if (!apb_read_u32(&r, &data_len)) return NULL;
        if (strcmp(cur, name) == 0) {
            *len = data_len;
            return r.p;
        }
        r.p += data_len;
        if (r.p > r.end) return NULL;
    }
    return NULL;
}

static uint16_t embedded_version(void) {
    if (s_embedded_version == 0) {
        apb_reader_t r = { pendant_default_bundle_start, pendant_default_bundle_end };
        uint16_t version = 0, count = 0;
        if (memcmp(r.p, "APB1", 4) == 0) {
            r.p += 4;
            if (apb_read_u16(&r, &version) && apb_read_u16(&r, &count)) {
                s_embedded_version = version;
            }
        }
    }
    return s_embedded_version;
}

bool app_assets_init(void) {
    esp_vfs_littlefs_conf_t conf = {
        .partition_label = PART_LABEL,
        .base_path = MOUNT,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS 挂载失败: %s", esp_err_to_name(err));
        return false;
    }
    size_t total = 0, used = 0;
    esp_littlefs_info(PART_LABEL, &total, &used);
    ESP_LOGI(TAG, "素材区: %u/%u KiB", (unsigned)(used / 1024), (unsigned)(total / 1024));

    int installed = app_assets_installed_version();
    if (installed <= 0) {
        installed = install_embedded_bundle();
        if (installed > 0) app_store_save_bundle_version(installed);
    }
    if (installed <= 0) {
        ESP_LOGE(TAG, "无可用素材包");
        return false;
    }
    if (installed != app_store_bundle_version()) {
        app_store_save_bundle_version(installed);
    }
    s_use_embedded = (installed == (int)embedded_version());
    ESP_LOGI(TAG, "素材通道: %s", s_use_embedded ? "内嵌 flash 直读" : "文件系统");
    return true;
}

int app_assets_installed_version(void) {
    if (s_use_embedded) return (int)embedded_version();
    FILE *f = fopen(MOUNT "/bundle/manifest.json", "rb");
    if (!f) return -1;
    static char buf[2048];   // 开机主任务一次性路径,同样免栈
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    cJSON *root = cJSON_ParseWithLength(buf, n);
    if (!root) return -1;
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "version");
    int version = cJSON_IsNumber(v) ? v->valueint : -1;
    cJSON_Delete(root);
    return version;
}

// 依据 manifest 取文件名与帧参数,整文件读堆。
static bool load_frames(const char *file, uint16_t frames, uint16_t w, uint16_t h,
                        uint8_t fps, app_asset_frames_t *out) {
    memset(out, 0, sizeof(*out));
    if (!file || frames == 0 || w == 0 || h == 0) return false;
    size_t need = (size_t)frames * w * h * 2;

    if (s_use_embedded) {
        size_t len = 0;
        const uint8_t *p = embedded_file(file, &len);
        if (p && len == need) {
            out->data = (uint8_t *)p;   // flash 映射,只读,免堆
            out->frames = frames;
            out->w = w;
            out->h = h;
            out->fps = fps;
            out->from_flash = true;
            return true;
        }
        return false;
    }
    char path[96];
    snprintf(path, sizeof(path), MOUNT "/bundle/%s", file);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    // 校验文件长度与 manifest 一致,防错包/半包。
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size != (long)need) {
        fclose(f);
        return false;
    }
    uint8_t *data = malloc(need);
    if (!data) {
        fclose(f);
        return false;
    }
    if (fread(data, 1, need, f) != need) {
        free(data);
        fclose(f);
        return false;
    }
    fclose(f);
    out->data = data;
    out->frames = frames;
    out->w = w;
    out->h = h;
    out->fps = fps;
    return true;
}

// manifest 原文 2KB:必须静态 —— 本函数会被 LVGL 定时任务调到
// (宠物动作/天气切换),不能在任务栈上放大缓冲。
static char s_manifest_buf[2048];

static bool manifest_entry(const char *section, const char *name, int index,
                           char *file_out, size_t file_cap,
                           uint16_t *frames, uint16_t *w, uint16_t *h, uint8_t *fps) {
    cJSON *root = NULL;
    if (s_use_embedded) {
        // 内嵌模式连 manifest 也走 flash 直读,零文件 IO。
        size_t len = 0;
        const uint8_t *p = embedded_file("manifest.json", &len);
        if (p && len < sizeof(s_manifest_buf)) {
            memcpy(s_manifest_buf, p, len);
            root = cJSON_ParseWithLength(s_manifest_buf, len);
        }
    } else {
        FILE *f = fopen(MOUNT "/bundle/manifest.json", "rb");
        if (!f) return false;
        size_t n = fread(s_manifest_buf, 1, sizeof(s_manifest_buf) - 1, f);
        fclose(f);
        s_manifest_buf[n] = '\0';
        root = cJSON_ParseWithLength(s_manifest_buf, n);
    }
    if (!root) return false;

    cJSON *node = cJSON_GetObjectItemCaseSensitive(root, section);
    if (index >= 0 && strcmp(section, "decorations") == 0) {
        cJSON *it = NULL;
        int i = 0;
        cJSON_ArrayForEach(it, node) {
            if (i++ == index) { node = it; goto found; }
        }
        cJSON_Delete(root);
        return false;
    }
found:
    if (name && index < 0) node = cJSON_GetObjectItemCaseSensitive(node, name);
    bool ok = false;
    if (cJSON_IsObject(node)) {
        const cJSON *file = cJSON_GetObjectItemCaseSensitive(node, "file");
        const cJSON *fr = cJSON_GetObjectItemCaseSensitive(node, "frames");
        const cJSON *jw = cJSON_GetObjectItemCaseSensitive(node, "w");
        const cJSON *jh = cJSON_GetObjectItemCaseSensitive(node, "h");
        const cJSON *jfps = cJSON_GetObjectItemCaseSensitive(node, "fps");
        if (cJSON_IsString(file) && file->valuestring &&
            cJSON_IsNumber(jw) && cJSON_IsNumber(jh)) {
            strncpy(file_out, file->valuestring, file_cap - 1);
            file_out[file_cap - 1] = '\0';
            *frames = cJSON_IsNumber(fr) ? (uint16_t)fr->valueint : 1;
            *w = (uint16_t)jw->valueint;
            *h = (uint16_t)jh->valueint;
            *fps = cJSON_IsNumber(jfps) ? (uint8_t)jfps->valueint : 6;
            ok = *frames > 0 && *w > 0 && *h > 0;
        }
    }
    cJSON_Delete(root);
    return ok;
}

static bool load_from_manifest(const char *section, const char *name, int index,
                               app_asset_frames_t *out) {
    char file[64];
    uint16_t frames = 0, w = 0, h = 0;
    uint8_t fps = 6;
    if (!manifest_entry(section, name, index, file, sizeof(file), &frames, &w, &h, &fps)) {
        return false;
    }
    return load_frames(file, frames, w, h, fps, out);
}

bool app_assets_load_action(const char *name, app_asset_frames_t *out) {
    return load_from_manifest("actions", name, -1, out);
}

bool app_assets_load_map(app_asset_frames_t *out) {
    return load_from_manifest("map", NULL, -1, out);
}

bool app_assets_load_weather(const char *kind, app_asset_frames_t *out) {
    return load_from_manifest("weather", kind, -1, out);
}

bool app_assets_load_decoration(int index, app_asset_frames_t *out) {
    return load_from_manifest("decorations", NULL, index, out);
}

void app_asset_frames_free(app_asset_frames_t *frames) {
    if (!frames->from_flash) free(frames->data);
    memset(frames, 0, sizeof(*frames));
}

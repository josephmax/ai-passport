// main/app/app_assets.h —— 皮肤包(Asset Bundle)加载。
//
// 首次启动/素材区校验失败时,把固件内嵌的默认包(APB1,与
// tools/gen_assets.py 产出的 pendant_default_bundle.bin 同构)解包写入
// LittleFS;此后设备渲染只读文件系统中的当前版本(ADR-0002/0004)。
// 所有 load 把帧数据整块读进堆;调用方负责 app_asset_frames_free。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;      // 帧数据(LVGL 只读);可能是堆缓冲,也可能直指
                        // flash 映射区(from_flash=true,禁止 free)
    uint16_t frames;
    uint16_t w;
    uint16_t h;
    uint8_t fps;
    bool from_flash;
} app_asset_frames_t;

// 挂载 LittleFS 并确保当前版本素材可用。失败返回 false(UI 用纯色降级)。
bool app_assets_init(void);

// 当前已安装素材包版本(读 manifest,失败 -1)。
int app_assets_installed_version(void);

// 动作名: "run" | "fight" | "sleep" | "victory";天气: "rain" | "snow"。
bool app_assets_load_action(const char *name, app_asset_frames_t *out);
bool app_assets_load_map(app_asset_frames_t *out);
bool app_assets_load_weather(const char *kind, app_asset_frames_t *out);
bool app_assets_load_decoration(int index, app_asset_frames_t *out);

void app_asset_frames_free(app_asset_frames_t *frames);

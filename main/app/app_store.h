// main/app/app_store.h —— 应用持久化(NVS)统一入口。
// 设备只持久化:四项本机设置、番茄状态、经验账本、最后快照、
// 网络三要素(Wi-Fi/服务地址/令牌)、素材版本(ADR-0001/配置中心规格 §1)。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_focus.h"
#include "app_snapshot.h"
#include "app_time.h"
#include "app_xp.h"

#define APP_NET_SSID_MAX 33
#define APP_NET_PASS_MAX 65
#define APP_NET_URL_MAX 96
#define APP_DEV_TOKEN_MAX 65

typedef struct {
    uint8_t brightness;      // 1..5 档
    uint8_t volume;          // 0..5 档
    uint16_t auto_off_s;     // 自动息屏秒数:15/30/60/120
    uint16_t rest_start_min; // 作息起始分钟(含)
    uint16_t rest_end_min;   // 作息结束分钟(不含)
} app_settings_t;

typedef struct {
    char ssid[APP_NET_SSID_MAX];
    char password[APP_NET_PASS_MAX];
    char service_url[APP_NET_URL_MAX];   // 如 http://192.168.1.10:3000
    char device_token[APP_DEV_TOKEN_MAX];
} app_net_cfg_t;

// 打开 NVS 句柄(幂等)。失败则后续读写全部安全降级为内存态。
void app_store_init(void);

app_settings_t app_store_settings(void);
void app_store_save_settings(const app_settings_t *settings);

// 最后落盘的快照原文(JSON);无则返回 false。max 含终止符。
bool app_store_snapshot_json(char *buf, size_t cap);
bool app_store_save_snapshot_json(const char *json);

app_focus_state_t app_store_focus(void);
void app_store_save_focus(const app_focus_state_t *focus);
uint8_t app_store_focus_preset(void);
void app_store_save_focus_preset(uint8_t units);

app_xp_store_t app_store_xp(void);
void app_store_save_xp(const app_xp_store_t *xp);

bool app_store_net(app_net_cfg_t *out);
bool app_store_save_net(const app_net_cfg_t *net);

int app_store_bundle_version(void);            // -1 = 未安装
void app_store_save_bundle_version(int v);

int64_t app_store_last_sync_ms(void);          // 0 = 从未同步
void app_store_save_last_sync_ms(int64_t ms);

// main/app/app_runtime.h —— 跨模块共享的运行时状态与事件。
//
// 单例 app_runtime():UI 只读它;同步/配网任务写它并广播事件。
// 事件走 ESP-IDF 事件循环(base APP_EVENT),回调里仍禁止重活。
#pragma once

#include "esp_event.h"

#include <stdbool.h>
#include <stdint.h>

#include "app_focus.h"
#include "app_snapshot.h"
#include "app_store.h"
#include "app_time.h"
#include "app_weather.h"
#include "app_xp.h"

ESP_EVENT_DECLARE_BASE(APP_EVENT);

typedef enum {
    APP_EVENT_SNAPSHOT_UPDATED = 1,   // snap 快照有新数据(或首次加载)
    APP_EVENT_WIFI_STATE,             // wifi_connected 变化
    APP_EVENT_FOCUS_CHANGED,          // 番茄状态变化(start/add/cancel/restore)
    APP_EVENT_XP_CHANGED,             // 经验/今日/本周变化
    APP_EVENT_FOCUS_VICTORY,          // 全部单元结束:Shell 跳宠物页
    APP_EVENT_SCREEN_OFF,             // 电源管理要求熄屏
    APP_EVENT_SCREEN_ON,              // 唤醒
    APP_EVENT_SYNC_STATE,             // 同步进行中/结束(暂停帧动画加载)
} app_event_id_t;

typedef struct {
    app_snapshot_t snap;
    bool snap_valid;
    int64_t snap_received_at_ms;      // 本机收到快照的时刻(离线标注的参照)
    bool wifi_connected;
    bool paired;                      // 有令牌即视为已配对
    char service_url[96];
    bool sync_in_progress;
    // 番茄与经验的活动镜像(权威数据在 store,UI 免锁读这一份)
    app_focus_state_t focus;
    app_xp_store_t xp;
    app_rest_window_t rest;           // 生效中的作息窗口
    app_settings_t settings;
    bool assets_ok;                   // 素材区可用(否则 UI 纯色降级)
    // 天气当前态(由快照推导,宠物页读取)
    int weather_code;                 // WMO;离线沿用最后已知
    int sunrise_min, sunset_min;
    app_tod_t tod;                    // 带滞回的时段(壳里周期推进)
} app_runtime_t;

app_runtime_t *app_runtime(void);

void app_runtime_publish(app_event_id_t id);   // 持有者写完状态后广播

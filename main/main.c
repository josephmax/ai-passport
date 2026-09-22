// main/main.c —— 多合一挂坠应用入口。
//
// 启动顺序:BSP 显示/LVGL → 存储(NVS) → 运行时状态恢复(设置/经验/
// 番茄/最后快照) → 素材区(LittleFS+默认包) → 音效 → 外壳(三页+按键)
// → 周期服务 → 电源管理(息屏/浅睡/深睡)。
#include "app_audio_fx.h"
#include "app_assets.h"
#include "app_debug.h"
#include "app_focus.h"
#include "app_net.h"
#include "app_power.h"
#include "app_sync.h"
#include "app_runtime.h"
#include "app_service.h"
#include "app_store.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "ui_shell.h"
#include "ui_theme.h"

#include <string.h>
#include <sys/time.h>

static const char *TAG = "pendant";

// 把 NVS 里的持久态装回运行时;番茄状态做合理性防御。
static void runtime_restore(void) {
    app_runtime_t *rt = app_runtime();
    rt->settings = app_store_settings();
    rt->rest.start_min = rt->settings.rest_start_min;
    rt->rest.end_min = rt->settings.rest_end_min;
    rt->xp = app_store_xp();
    rt->focus = app_store_focus();

    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t now = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    if (rt->focus.running &&
        (rt->focus.units == 0 || rt->focus.units > APP_FOCUS_MAX_UNITS ||
         rt->focus.end_at_ms <= now - 60 * 1000 ||
         rt->focus.end_at_ms > now + (int64_t)APP_FOCUS_MAX_UNITS * APP_FOCUS_UNIT_MS + 60 * 1000)) {
        // 脏数据(掉电瞬间/版本残留):丢弃计时,经验账本不动。
        ESP_LOGW(TAG, "番茄状态异常,丢弃(剩余过旧或越界)");
        app_focus_cancel(&rt->focus);
        app_store_save_focus(&rt->focus);
    }

    app_net_cfg_t net;
    if (app_store_net(&net)) {
        rt->paired = net.device_token[0] != '\0';
        strlcpy(rt->service_url, net.service_url, sizeof(rt->service_url));
    }

    // 4KB 快照缓冲必须放静态区:主任务栈容不下(默认 3584B),栈上开大会
    // 写穿金丝雀 → Stack protection fault(首次真机刷写即中招)。
    static char json[4096];
    if (app_store_snapshot_json(json, sizeof(json))) {
        app_snapshot_t snap;
        if (app_snapshot_parse(json, strlen(json), &snap)) {
            rt->snap = snap;
            rt->snap_valid = true;
            rt->snap_received_at_ms = now;
            rt->weather_code = snap.weather_code;
            if (snap.sunrise_min > 0 && snap.sunset_min > snap.sunrise_min) {
                rt->sunrise_min = snap.sunrise_min;
                rt->sunset_min = snap.sunset_min;
            }
        }
    }
    if (rt->sunrise_min == 0) {   // 从未同步:合理缺省(6:30/18:30)
        rt->sunrise_min = 6 * 60 + 30;
        rt->sunset_min = 18 * 60 + 30;
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "多合一挂坠启动(wakeup=%d)", (int)esp_sleep_get_wakeup_cause());
    app_debug_boot_report();

    // 事件循环必须先于任何 publish/注册(app_runtime/ui_shell 都依赖)。
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    app_store_init();
    app_service_init();   // 恢复最后已知墙钟(若有)

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示初始化失败,应用无法继续");
        return;
    }

    runtime_restore();
    ui_theme_brightness_apply(app_runtime()->settings.brightness);

    app_runtime()->assets_ok = app_assets_init();
    if (!app_runtime()->assets_ok) {
        ESP_LOGW(TAG, "素材区不可用,宠物页将纯色降级");
    }

    if (!app_audio_fx_init()) {
        ESP_LOGW(TAG, "音效不可用,静音降级");
    }
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用,状态条将隐藏电量");
    }

    ui_shell_init();
    ui_shell_boot_refresh();

    app_net_init();
    app_sync_init();

    if (!app_power_init()) {
        ESP_LOGW(TAG, "电源管理任务启动失败(息屏/睡眠不可用)");
    }
    ESP_LOGI(TAG, "就绪");
}

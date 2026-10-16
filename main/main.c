// main/main.c —— 多合一挂坠应用入口。
//
// 启动顺序:BSP 显示/LVGL → 存储(NVS) → 运行时状态恢复(设置/经验/
// 番茄/最后快照) → 素材区(LittleFS+默认包) → 音效 → 外壳(主屏+按键)
// → 周期服务 → 电源管理(息屏/浅睡/深睡)。
#include "app_audio_fx.h"
#include "app_assets.h"
#include "app_debug.h"
#include "app_focus.h"
#include "app_ble.h"
#include "app_power.h"
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
    rt->focus_preset = app_store_focus_preset();
    (void)app_store_badge(&rt->badge);

    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t now = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    if (rt->focus.running &&
        (rt->focus.units == 0 || rt->focus.units > APP_FOCUS_MAX_UNITS ||
         rt->focus.xp_granted > rt->focus.units || rt->focus.end_at_ms <= 0 ||
         rt->focus.end_at_ms > now + (int64_t)APP_FOCUS_MAX_UNITS * APP_FOCUS_UNIT_MS + 60 * 1000)) {
        // 脏数据(掉电瞬间/版本残留):丢弃计时,经验账本不动。
        ESP_LOGW(TAG, "番茄状态异常,丢弃(字段越界)");
        app_focus_cancel(&rt->focus);
        app_store_save_focus(&rt->focus);
    }

    // BLE restores only a cache whose device and selected host both match.
#define TEST_SCENE 0   // 临时:强制白天+下雨验证天气精灵/地图滚动;验收后置 0
#if TEST_SCENE
    rt->weather_code = 63;              // 雨
    rt->sunrise_min = 0;                // 全天白天 → 地图滚动、跑步动作
    rt->sunset_min = 1439;
    rt->rest.start_min = rt->rest.end_min = 0;   // 作息禁用,不睡觉
    rt->tod = APP_TOD_DAY;
#endif
    if (rt->sunrise_min == 0 && !TEST_SCENE) {   // 从未同步:合理缺省(6:30/18:30)
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

    /* Initialize the radio before Home starts its rendering/animation timers.
     * On a single C3 core their combined cold-start work can starve IDLE. The
     * initial UI reads the restored BLE source/status after registration. */
    if (!app_ble_init()) ESP_LOGE(TAG, "BLE initialization failed");

    ui_shell_init();
    ui_shell_boot_refresh();

    if (!app_power_init()) {
        ESP_LOGW(TAG, "电源管理任务启动失败(息屏/睡眠不可用)");
    }
    ESP_LOGI(TAG, "就绪");
}

// main/app/app_service.c —— 周期维护服务实现。
#include "app_service.h"

#include "app_focus.h"
#include "app_runtime.h"
#include "app_store.h"
#include "app_time.h"
#include "app_xp.h"
#include "esp_log.h"
#include "nvs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sys/time.h"

#include <string.h>

static const char *TAG = "service";
static const char *KEY_CLOCK = "clock";

static SemaphoreHandle_t s_lock;
static int64_t s_last_clock_save_ms;

static int64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void app_service_init(void) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    // 恢复冻结的最后已知墙钟,避免 1970 时间下的日界误判。
    int64_t saved = 0;
    nvs_handle_t h;
    if (nvs_open("pendant", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i64(h, KEY_CLOCK, &saved);
        nvs_close(h);
    }
    if (saved >= APP_TIME_PLAUSIBLE_S * 1000) {
        // 仅当影子比当前系统钟更新才覆盖:深睡唤醒后 RTC 钟已自然推进,
        // 无条件回拨会造成"越睡越慢"的时钟漂移(曾致白天误判为作息时段)。
        struct timeval cur;
        gettimeofday(&cur, NULL);
        if ((int64_t)cur.tv_sec * 1000 + cur.tv_usec / 1000 < saved) {
            struct timeval tv = { .tv_sec = saved / 1000, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "恢复最后已知墙钟: %lld", (long long)(saved / 1000));
        } else {
            ESP_LOGI(TAG, "系统钟已新于影子,保留 RTC 推进结果");
        }
    }
#ifdef PENDANT_BOOT_EPOCH
    // 调试构建专用(编译机注入本地历元):未配网设备的钟从未校准,与基准
    // 偏差超 1 小时即重置。正式构建不定义此宏;配网后由服务校时接管。
    {
        struct timeval cur;
        gettimeofday(&cur, NULL);
        int64_t diff = (int64_t)cur.tv_sec - (int64_t)PENDANT_BOOT_EPOCH;
        if (diff > 3600 || diff < -3600) {
            struct timeval tv = { .tv_sec = PENDANT_BOOT_EPOCH, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            ESP_LOGW(TAG, "调试对时: 时钟偏差%llds,已重置为构建基准",
                     (long long)diff);
        }
    }
#endif
}

static void service_run(int64_t now_local_ms, int configure_units) {
    if (!s_lock) return;
    int64_t now_s = now_local_ms / 1000;
    bool focus_changed = false, xp_changed = false, victory = false;
    bool clock_valid = now_s >= APP_TIME_PLAUSIBLE_S;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    app_runtime_t *rt = app_runtime();

    // 1) 番茄推进:先 drain 静默经验,最后吐胜利。
    app_focus_ev_t ev;
    int guard = 0;
    while ((ev = app_focus_poll(&rt->focus, now_local_ms)) != APP_FOCUS_EV_NONE &&
           guard++ < 16) {
        if (ev == APP_FOCUS_EV_UNIT_DONE) {
            if (clock_valid) {
                app_xp_rollover(&rt->xp, app_time_day_index(now_s),
                                app_time_week_index(now_s));
            }
            app_xp_add_unit(&rt->xp);
            // 完成链路观测点:经验是否真的入账(规格 §11)。
            ESP_LOGI(TAG, "番茄单元完成: 经验+1 总=%u 今=%u 周=%u",
                     (unsigned)rt->xp.total_xp, (unsigned)rt->xp.today_count,
                     (unsigned)rt->xp.week_count);
            xp_changed = true;
            focus_changed = true;
        } else if (ev == APP_FOCUS_EV_VICTORY) {
            ESP_LOGI(TAG, "番茄全部完成: 发布胜利事件");
            victory = true;
            focus_changed = true;
        }
    }
    if (configure_units >= 0 && configure_units <= APP_FOCUS_MAX_UNITS) {
        app_focus_configure(&rt->focus, (uint8_t)configure_units, now_local_ms);
        rt->focus_preset = (uint8_t)configure_units;
        app_store_save_focus_preset(rt->focus_preset);
        focus_changed = true;
        victory = false; // A confirmed replacement supersedes the old finish event.
    }
    if (focus_changed) app_store_save_focus(&rt->focus);
    if (xp_changed) app_store_save_xp(&rt->xp);

    // 2) 跨日/跨周回零(仅时钟可信时)。
    if (clock_valid && !rt->focus.running) {
        if (app_xp_rollover(&rt->xp, app_time_day_index(now_s),
                            app_time_week_index(now_s))) {
            app_store_save_xp(&rt->xp);
            xp_changed = true;
        }
    }

    // 3) 时段滞回推进(宠物页只读结果)。
    if (clock_valid && rt->sunrise_min > 0 && rt->sunset_min > rt->sunrise_min) {
        rt->tod = app_tod_compute(app_time_min_of_day(now_s),
                                  rt->sunrise_min, rt->sunset_min, rt->tod);
    } else {
        rt->tod = APP_TOD_NIGHT;   // 无时钟/无日出日落数据:按夜间呈现
    }

    // 4) 墙钟影子:10 分钟一次,深睡前另有一次强制写。
    if (clock_valid &&
        now_local_ms - s_last_clock_save_ms > 10 * 60 * 1000) {
        nvs_handle_t h;
        if (nvs_open("pendant", NVS_READWRITE, &h) == ESP_OK) {
            if (nvs_set_i64(h, KEY_CLOCK, now_local_ms) == ESP_OK) {
                (void)nvs_commit(h);
            }
            nvs_close(h);
        }
        s_last_clock_save_ms = now_local_ms;
    }
    xSemaphoreGive(s_lock);

    if (focus_changed) app_runtime_publish(APP_EVENT_FOCUS_CHANGED);
    if (victory) app_runtime_publish(APP_EVENT_FOCUS_VICTORY);
    if (xp_changed) app_runtime_publish(APP_EVENT_XP_CHANGED);
}

void app_service_tick(int64_t now_local_ms) {
    service_run(now_local_ms, -1);
}

void app_service_configure_focus(uint8_t units, int64_t now_local_ms) {
    if (units <= APP_FOCUS_MAX_UNITS) service_run(now_local_ms, units);
}

void app_service_flush_clock(void) {
    int64_t now = now_ms();
    if (now < APP_TIME_PLAUSIBLE_S * 1000) return;
    nvs_handle_t h;
    if (nvs_open("pendant", NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_i64(h, KEY_CLOCK, now) == ESP_OK) (void)nvs_commit(h);
        nvs_close(h);
    }
}

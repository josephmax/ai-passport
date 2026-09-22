// main/ui/ui_shell.c —— 外壳实现。
//
// 键位(规格 §3):三页间 UP/DOWN 循环;OK 短按按页语义(仪表盘下钻/
// 宠物启停番茄/设置进入调整);OK 长按(宠物取消全部/下钻返回/设置退出
// 子视图或退出设置页)。息屏唤醒回到原页;胜利时任意页跳宠物页。
#include "ui_shell.h"

#include "app_audio_fx.h"
#include "app_focus.h"
#include "app_power.h"
#include "app_runtime.h"
#include "app_service.h"
#include "app_store.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ui_dash.h"
#include "ui_pet.h"
#include "ui_settings.h"
#include "ui_theme.h"

#include <sys/time.h>

static const char *TAG = "shell";

typedef enum { PAGE_DASH = 0, PAGE_PET, PAGE_SETTINGS, PAGE_COUNT } page_t;

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

// 深睡重启后回到息屏前所在页(RTC 快存随深睡保留)。
static RTC_DATA_ATTR uint32_t s_rtc_magic;
static RTC_DATA_ATTR uint8_t s_rtc_page;
#define RTC_MAGIC 0x50454E44   // "PEND"

static page_t s_page;
static QueueHandle_t s_input_queue;
static lv_timer_t *s_second_timer;

static int64_t wall_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void set_page(page_t page) {
    s_page = page;
    s_rtc_page = (uint8_t)page;
    lv_obj_t *scr = page == PAGE_DASH ? ui_dash_screen()
                  : page == PAGE_PET ? ui_pet_screen()
                                     : ui_settings_screen();
    lv_screen_load(scr);
    ui_page_dots_t *dots = page == PAGE_DASH ? ui_dash_dots()
                      : page == PAGE_PET ? ui_pet_dots()
                                         : ui_settings_dots();
    ui_theme_page_dots_set(dots, (int)page);
}

static void refresh_current(void) {
    if (s_page == PAGE_DASH) ui_dash_refresh();
    else if (s_page == PAGE_PET) ui_pet_refresh();
    else ui_settings_refresh();
}

// ---- 番茄键位(宠物页):锚点一律用墙钟(跨深睡) ----
static void pet_ok_short(void) {
    app_runtime_t *rt = app_runtime();
    int64_t now = wall_now_ms();
    app_focus_ev_t ev = rt->focus.running
        ? app_focus_add_unit(&rt->focus, now)
        : app_focus_start(&rt->focus, now);
    app_store_save_focus(&rt->focus);
    if (ev == APP_FOCUS_EV_STACK_FULL) {
        app_audio_fx_play(APP_FX_BEEP);
        return;
    }
    app_runtime_publish(APP_EVENT_FOCUS_CHANGED);
}

static void pet_ok_long(void) {
    app_runtime_t *rt = app_runtime();
    if (!rt->focus.running) return;
    app_focus_cancel(&rt->focus);
    app_store_save_focus(&rt->focus);
    app_runtime_publish(APP_EVENT_FOCUS_CHANGED);
}

static void process_input(const input_event_t *in) {
    bool ok_short = in->btn == BSP_BTN_OK && in->event == BSP_BTN_CLICK;
    bool ok_long = in->btn == BSP_BTN_OK && in->event == BSP_BTN_LONG;
    bool up = in->btn == BSP_BTN_UP && in->event == BSP_BTN_CLICK;
    bool down = in->btn == BSP_BTN_DOWN && in->event == BSP_BTN_CLICK;

    app_power_notify_activity();
    if (!ok_short && !ok_long && !up && !down) return;

    // 输入任务不是 LVGL 任务:所有触达 LVGL 的页面键处理必须持锁
    // (仓库硬规则),否则与渲染任务竞态可能损坏 LVGL 池。
    if (!bsp_lvgl_lock(500)) return;

    if (s_page == PAGE_DASH) {
        if (ui_dash_key(ok_short, ok_long, up, down)) return;
    } else if (s_page == PAGE_PET) {
        if (ok_short) { pet_ok_short(); return; }
        if (ok_long) { pet_ok_long(); return; }
    } else {
        // 设置页:一级上下留给切页(由末尾分支处理),其余由页面层级消费。
        if (ui_settings_key(ok_short, ok_long, up, down)) return;
    }
    if (up || down) {
        set_page((page_t)((s_page + (up ? PAGE_COUNT - 1 : 1)) % PAGE_COUNT));
    }
    bsp_lvgl_unlock();
}

static void input_task(void *arg) {
    (void)arg;
    input_event_t in;
    for (;;) {
        if (xQueueReceive(s_input_queue, &in, portMAX_DELAY) == pdTRUE) {
            process_input(&in);
        }
    }
}

// 按键回调运行于共享 esp_timer 任务:只入队。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (!s_input_queue) return;
    const input_event_t in = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &in, 0);
}

// ---- 事件联动 ----
static void on_app_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)data;
    switch ((app_event_id_t)id) {
    case APP_EVENT_FOCUS_VICTORY:
        if (bsp_lvgl_lock(300)) {
            set_page(PAGE_PET);
            ui_pet_on_victory();
            ui_pet_refresh();
            bsp_lvgl_unlock();
        }
        app_audio_fx_play(APP_FX_VICTORY);
        break;
    case APP_EVENT_SCREEN_OFF:
        bsp_display_backlight(0);
        ui_pet_set_paused(true);
        break;
    case APP_EVENT_SCREEN_ON:
        ui_pet_set_paused(false);
        if (bsp_lvgl_lock(300)) {
            refresh_current();
            bsp_lvgl_unlock();
        }
        ui_theme_brightness_apply(app_runtime()->settings.brightness);
        break;
    case APP_EVENT_SNAPSHOT_UPDATED:
    case APP_EVENT_WIFI_STATE:
    case APP_EVENT_FOCUS_CHANGED:
    case APP_EVENT_XP_CHANGED:
        if (bsp_lvgl_lock(200)) {
            refresh_current();
            bsp_lvgl_unlock();
        }
        break;
    default:
        break;
    }
}

// ---- 1Hz 节拍:服务推进 + 当前页刷新 ----
static void second_tick(lv_timer_t *t) {
    (void)t;
    app_service_tick(wall_now_ms());
    refresh_current();
}

void ui_shell_init(void) {
    // 三页常驻创建;宠物页素材最重,失败时页面内部纯色降级。
    if (bsp_lvgl_lock(2000)) {
        ui_dash_create();
        ui_settings_create();
        ui_pet_create();
        page_t start = PAGE_DASH;
        if (s_rtc_magic == RTC_MAGIC &&
            esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
            start = s_rtc_page < PAGE_COUNT ? (page_t)s_rtc_page : PAGE_DASH;
        }
        s_rtc_magic = RTC_MAGIC;
        set_page(start);
        bsp_lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "LVGL 锁超时,页面未创建");
        return;
    }

    s_input_queue = xQueueCreate(8, sizeof(input_event_t));
    TaskHandle_t task = NULL;
    if (s_input_queue &&
        xTaskCreate(input_task, "shell_input", 4096, NULL, 5, &task) == pdPASS) {
        esp_err_t err = bsp_button_init(on_key, NULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "输入分发任务创建失败");
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        APP_EVENT, ESP_EVENT_ANY_ID, on_app_event, NULL, NULL));
    s_second_timer = lv_timer_create(second_tick, 1000, NULL);
}

void ui_shell_boot_refresh(void) {
    if (bsp_lvgl_lock(500)) {
        refresh_current();
        bsp_lvgl_unlock();
    }
    // 堆水位观测:开机后基线,供真机验收判断内存预算(规格 §11)。
    ESP_LOGI(TAG, "堆水位: free=%u 最小曾=%u (LVGL池为独立静态区)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT));
}

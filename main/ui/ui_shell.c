// One home screen; usage/settings are second-level lists. Only worker tasks
// persist focus/settings; the LVGL timer refreshes display state only.
#include "ui_shell.h"
#include "app_audio_fx.h"
#include "app_debug.h"
#include "app_home.h"
#include "app_power.h"
#include "app_prov.h"
#include "app_runtime.h"
#include "app_service.h"
#include "app_store.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ui_dash.h"
#include "ui_pet.h"
#include "ui_settings.h"
#include "ui_theme.h"
#include <sys/time.h>

static const char *TAG = "shell";
typedef enum { PAGE_HOME, PAGE_USAGE, PAGE_SETTINGS } page_t;
typedef struct { bsp_btn_t btn; bsp_btn_ev_t event; bool wake_only; } input_event_t;
static page_t s_page;
static app_home_t s_home;
static QueueHandle_t s_input_queue;

static int64_t wall_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void refresh_current(void) {
    if (s_page == PAGE_HOME) ui_pet_set_navigation(&s_home);
    else if (s_page == PAGE_USAGE) ui_dash_refresh();
    else ui_settings_refresh();
}

static void set_page(page_t page) {
    // Always load the resident home before deleting an active secondary screen.
    lv_screen_load(ui_pet_screen());
    if (s_page == PAGE_USAGE) ui_dash_destroy();
    else if (s_page == PAGE_SETTINGS) ui_settings_destroy();
    s_page = page;
    if (page == PAGE_USAGE) lv_screen_load(ui_dash_create()->screen);
    else if (page == PAGE_SETTINGS) lv_screen_load(ui_settings_create()->screen);
    ui_pet_set_paused(page != PAGE_HOME || app_power_screen_off());
    refresh_current();
}

static void go_home(void) {
    app_home_reset(&s_home);
    set_page(PAGE_HOME);
}

static void flush_settings_work(void) {
    ui_settings_work_t work = {0};
    if (bsp_lvgl_lock(100)) {
        work = ui_settings_take_work();
        bsp_lvgl_unlock();
    }
    if (work.save) app_store_save_settings(&work.settings);
    if (work.cancel_provisioning) app_prov_cancel();
}

static void process_input(const input_event_t *in) {
    bool ok_short = in->btn == BSP_BTN_OK && in->event == BSP_BTN_CLICK;
    bool ok_long = in->btn == BSP_BTN_OK && in->event == BSP_BTN_LONG;
    bool up = in->btn == BSP_BTN_UP && in->event == BSP_BTN_CLICK;
    bool down = in->btn == BSP_BTN_DOWN && in->event == BSP_BTN_CLICK;
    if (!ok_short && !ok_long && !up && !down) return;
    bool wake_only = in->wake_only || app_power_screen_off();
    app_power_notify_activity();
    if (!bsp_lvgl_lock(500)) return;
    if (wake_only) {
        go_home(); // A wake gesture never also confirms/restarts a timer.
        bsp_lvgl_unlock();
        return;
    }
    if (ui_pet_victory_active()) { bsp_lvgl_unlock(); return; }
    app_audio_fx_stop(); // Preserve the user's interruptible audio behavior.
    app_home_action_t action = APP_HOME_NONE;
    uint8_t units = 0;
    if (s_page == PAGE_HOME) {
        action = app_home_key(&s_home, up ? APP_HOME_UP : down ? APP_HOME_DOWN : ok_long ? APP_HOME_BACK : APP_HOME_OK,
                               &app_runtime()->focus, app_runtime()->focus_preset);
        units = s_home.draft;
        if (action == APP_HOME_OPEN_USAGE) set_page(PAGE_USAGE);
        else if (action == APP_HOME_OPEN_SETTINGS) set_page(PAGE_SETTINGS);
        else refresh_current();
    } else if (s_page == PAGE_USAGE) {
        if (!ui_dash_key(ok_short, ok_long, up, down) && ok_long) set_page(PAGE_HOME);
    } else if (!ui_settings_key(ok_short, ok_long, up, down) && ok_long) set_page(PAGE_HOME);
    bsp_lvgl_unlock();
    if (action == APP_HOME_APPLY_FOCUS) app_service_configure_focus(units, wall_now_ms());
    else if (action == APP_HOME_LIMIT) app_audio_fx_play(APP_FX_BEEP);
}

static void input_task(void *arg) {
    (void)arg;
    input_event_t input;
    TickType_t last_tick = xTaskGetTickCount();
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, pdMS_TO_TICKS(250)) == pdTRUE) process_input(&input);
        flush_settings_work();
        TickType_t now = xTaskGetTickCount();
        if (now - last_tick >= pdMS_TO_TICKS(1000)) {
            // No LVGL lock while NVS commits. Power-task polling shares the service mutex.
            app_service_tick(wall_now_ms());
            last_tick = now;
        }
    }
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (!s_input_queue) return;
    const input_event_t input = {.btn = btn, .event = ev, .wake_only = app_power_screen_off()};
    (void)xQueueSend(s_input_queue, &input, 0);
}

static void on_app_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)data;
    if (!bsp_lvgl_lock(500)) return;
    switch ((app_event_id_t)id) {
    case APP_EVENT_FOCUS_VICTORY:
        // Ignore a delayed finish event after a new timer has been confirmed.
        if (app_runtime()->focus.running) break;
        ESP_LOGI(TAG, "胜利事件: 返回主屏+动画+音效");
        app_power_wake_screen();
        go_home(); // settings destroy restores an interrupted live adjustment
        ui_pet_set_paused(false);
        ui_pet_on_victory();
        ui_pet_refresh();
        app_audio_fx_play(APP_FX_VICTORY);
        break;
    case APP_EVENT_SCREEN_OFF:
        bsp_display_backlight(0);
        ui_pet_set_paused(true);
        break;
    case APP_EVENT_SCREEN_ON:
        go_home();
        ui_theme_brightness_apply(app_runtime()->settings.brightness);
        break;
    case APP_EVENT_SNAPSHOT_UPDATED:
    case APP_EVENT_WIFI_STATE:
    case APP_EVENT_FOCUS_CHANGED:
    case APP_EVENT_XP_CHANGED:
    case APP_EVENT_SYNC_STATE:
        refresh_current();
        break;
    default: break;
    }
    bsp_lvgl_unlock();
}

static void second_tick(lv_timer_t *timer) {
    (void)timer;
    if (!app_power_screen_off()) refresh_current();
}

void ui_shell_init(void) {
    if (!bsp_lvgl_lock(2000)) { ESP_LOGE(TAG, "主屏创建锁超时"); return; }
    ui_pet_create();
    app_home_reset(&s_home);
    s_page = PAGE_HOME;
    set_page(PAGE_HOME); // Cold boot and deep-sleep wake always land here.
    lv_timer_create(second_tick, 1000, NULL);
    bsp_lvgl_unlock();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(APP_EVENT, ESP_EVENT_ANY_ID, on_app_event, NULL, NULL));
    s_input_queue = xQueueCreate(8, sizeof(input_event_t));
    TaskHandle_t task = NULL;
    if (s_input_queue && xTaskCreate(input_task, "shell_input", 4096, NULL, 5, &task) == pdPASS) {
        app_debug_register("shell_input", task);
        esp_err_t err = bsp_button_init(on_key, NULL);
        if (err != ESP_OK) ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(err));
    } else ESP_LOGE(TAG, "输入任务创建失败");
}

void ui_shell_boot_refresh(void) {
    if (bsp_lvgl_lock(500)) { refresh_current(); bsp_lvgl_unlock(); }
    ESP_LOGI(TAG, "堆水位: free=%u 最小曾=%u (LVGL池独立)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT));
}

// main/app/app_power.c —— 电源管理实现。
#include "app_power.h"

#include "app_focus.h"
#include "app_prov.h"
#include "app_runtime.h"
#include "app_service.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys/time.h"

static const char *TAG = "power";

// 三键共用 GPIO0(ADC 分压):松开=外部 10k 上拉拉高;按任意键拉低。
// 因此 GPIO0 低电平 = "任意按键",恰好是两种睡眠的唤醒条件。
#define BTN_WAKE_GPIO GPIO_NUM_0
#define LIGHT_SLICE_MAX_MS (60 * 1000)
#define DEEP_SLEEP_GRACE_MS (10 * 1000)

static TaskHandle_t s_task;
static volatile bool s_screen_off;
static int64_t s_last_activity_ms;
static int64_t s_screen_off_at_ms;

// 胜利发生在熄屏浅睡期间:电源任务把屏幕点亮让动画音效落地。
static void wake_screen(void) {
    if (!s_screen_off) return;
    s_screen_off = false;
    app_runtime_publish(APP_EVENT_SCREEN_ON);
}

void app_power_notify_activity(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    s_last_activity_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    wake_screen();
}

bool app_power_screen_off(void) {
    return s_screen_off;
}

static int64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// 浅睡一片:GPIO0 低电平或定时器唤醒。USB 控制台占用导致浅睡失败时
// 退化为普通延时(背光已关,仍然省电)。
static void light_sleep_slice(int64_t wake_in_ms) {
    if (wake_in_ms < 50) return;
    esp_err_t err = ESP_OK;
    err = gpio_wakeup_enable(BTN_WAKE_GPIO, GPIO_INTR_LOW_LEVEL);
    if (err == ESP_OK) err = esp_sleep_enable_gpio_wakeup();
    if (err == ESP_OK) err = esp_sleep_enable_timer_wakeup(wake_in_ms * 1000);
    if (err == ESP_OK) {
        err = esp_light_sleep_start();
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
        (void)gpio_wakeup_disable(BTN_WAKE_GPIO);
    }
    if (err != ESP_OK) {
        static bool warned;
        if (!warned) {
            ESP_LOGW(TAG, "浅睡不可用(%s),退化为延时", esp_err_to_name(err));
            warned = true;
        }
        vTaskDelay(pdMS_TO_TICKS(wake_in_ms > 1000 ? 1000 : wake_in_ms));
    }
}

// 深睡不归:按 demo_low_power 的既定顺序停外设后断电。
static void deep_sleep_now(void) {
    ESP_LOGI(TAG, "进入深睡(GPIO0 唤醒)");
    app_service_flush_clock();
    esp_err_t err = esp_deep_sleep_enable_gpio_wakeup(1ULL << BTN_WAKE_GPIO,
                                                      ESP_GPIO_WAKEUP_GPIO_LOW);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "深睡唤醒源配置失败: %s", esp_err_to_name(err));
    }
    // CW2017 与 ES8311 共线,先电量计后 codec;失败仅告警继续(既定策略)。
    (void)bsp_battery_sleep();
    (void)bsp_audio_sleep();
    (void)bsp_audio_prepare_deep_sleep();
    (void)bsp_i2c_prepare_deep_sleep();
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "无法停止 LVGL 刷屏,重启恢复");
        esp_restart();
    }
    (void)bsp_display_prepare_deep_sleep();
    esp_deep_sleep_start();
    ESP_LOGE(TAG, "esp_deep_sleep_start 意外返回,重启");
    esp_restart();
}

static void power_task(void *arg) {
    (void)arg;
    // 开机即视为一次活动。
    s_last_activity_ms = now_ms();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        app_runtime_t *rt = app_runtime();
        int64_t now = now_ms();
        int64_t idle_ms = now - s_last_activity_ms;
        uint16_t auto_off_ms = (uint16_t)(rt->settings.auto_off_s * 1000);

        if (!s_screen_off) {
            if (idle_ms >= auto_off_ms) {
                s_screen_off = true;
                s_screen_off_at_ms = now;
                app_runtime_publish(APP_EVENT_SCREEN_OFF);
            }
            continue;
        }

        // ---- 熄屏后的路径 ----
        // 配网热点需要常开:等待手机提交表单期间不睡眠。
        if (app_prov_state() != APP_PROV_IDLE) {
            static bool logged;
            if (!logged) { ESP_LOGI(TAG, "配网进行中,暂不睡眠"); logged = true; }
            continue;
        }
        if (rt->focus.running) {
            int64_t boundary = app_focus_next_boundary_ms(&rt->focus, now);
            int64_t slice = boundary < 0 ? LIGHT_SLICE_MAX_MS : boundary;
            if (slice <= 0) slice = 200;   // 已到点:马上醒去发经验/胜利
            if (slice > LIGHT_SLICE_MAX_MS) slice = LIGHT_SLICE_MAX_MS;
            light_sleep_slice(slice);
            app_service_tick(now_ms());
            if (!rt->focus.running) wake_screen();   // 胜利:亮屏播动画
            continue;
        }
        if (now - s_screen_off_at_ms >= DEEP_SLEEP_GRACE_MS &&
            !rt->sync_in_progress) {
            deep_sleep_now();   // 不返回
        }
    }
}

bool app_power_init(void) {
    if (s_task) return true;
    // GPIO0 同时被 ADC 按键使用;数字输入路径与 SARADC 采样互不冲突,
    // 这里只需保证浅睡时低电平唤醒可见。
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BTN_WAKE_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,   // 板上已有外部 10k 上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    if (xTaskCreate(power_task, "power", 3072, NULL, 3, &s_task) != pdPASS) {
        return false;
    }
    return true;
}

// main/app/app_power.c —— 电源管理实现。
#include "app_power.h"

#include "app_debug.h"
#include "app_focus.h"
#include "app_prov.h"
#include "app_runtime.h"
#include "app_service.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "soc/usb_serial_jtag_reg.h"
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

static int64_t now_ms(void);

// 胜利发生在熄屏期间(无论由电源任务还是 LVGL 秒节拍授经验):统一经此
// 亮屏。公开给 Shell 的胜利事件处理,修复"秒节拍先授经验却无人亮屏,
// 动画在黑屏里播完"的路径(USB 在位延时模式下该路径是主通道)。
void app_power_wake_screen(void) {
    if (!s_screen_off) return;
    s_screen_off = false;
    // 亮屏同时计入一次活动:熄屏判定只认 s_last_activity_ms,不刷新的话
    // 胜利亮屏会在下一个巡检周期(≤500ms)被重新熄掉,动画被掐断。
    s_last_activity_ms = now_ms();
    app_runtime_publish(APP_EVENT_SCREEN_ON);
}

void app_power_notify_activity(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    s_last_activity_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    app_power_wake_screen();
}

bool app_power_screen_off(void) {
    return s_screen_off;
}

static int64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// USB 主机在位探针:主机在位时每 1ms 发一帧 SOF,FRAM_NUM 寄存器持续递增;
// 无主机(电池场景)则静止。相隔 3ms 两次读数有变化即在位。
// 为什么需要它:C3 的 USB-Serial-JTAG 浅睡期间时钟全停,醒来重新枚举时
// 主机发出的 USB 总线复位会被硬件放大成整机复位(rst:0x15
// USB_UART_CHIP_RESET)——无声、无 panic。该复位一旦落在 NVS 提交途中,
// 会撕断写入使分区损坏,开机恢复只能整区擦除,经验/番茄/设置全灭
// (真机三轮复现,即"完成番茄后经验清零"根因)。主机在位时退化为延时:
// 台式调试由 USB 供电,不差这点电;电池场景无主机,浅睡照常省电。
static bool usb_host_attached(void) {
    uint32_t a = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG) & USB_SERIAL_JTAG_SOF_FRAME_INDEX;
    vTaskDelay(pdMS_TO_TICKS(3));
    uint32_t b = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG) & USB_SERIAL_JTAG_SOF_FRAME_INDEX;
    return a != b;
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
        // 唤醒源没配成绝不入睡:睡死无唤醒的设备"看起来像砖"。
        // 宁可亏掉深睡的省电,保住任何时刻可按键唤醒。
        ESP_LOGE(TAG, "深睡唤醒源配置失败,放弃深睡: %s", esp_err_to_name(err));
        return;
    }
    // CW2017 与 ES8311 共线,先电量计后 codec;失败仅告警继续(既定策略)。
    (void)bsp_battery_sleep();
    (void)bsp_audio_sleep();
    (void)bsp_audio_prepare_deep_sleep();
    (void)bsp_i2c_prepare_deep_sleep();
    // 拿不到锁说明渲染任务仍活跃:放弃本次深睡,保持清醒。
    // 此前此处写着重启"恢复",造成真机上"放着不动就自己重启"的幽灵死机。
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "深睡前无法停止 LVGL 刷屏,放弃深睡保持清醒");
        s_screen_off = false;
        return;
    }
    (void)bsp_display_prepare_deep_sleep();
    esp_deep_sleep_start();
    // 极意外的返回路径:同样不清醒时也绝不盲目重启。
    ESP_LOGE(TAG, "esp_deep_sleep_start 意外返回,保持运行");
    s_screen_off = false;
}

static void power_task(void *arg) {
    (void)arg;
    // 开机即视为一次活动。
    s_last_activity_ms = now_ms();
    int64_t last_soc_ms = -30000;   // 立即读第一次
    int64_t last_beat_ms = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        {
            int64_t now0 = now_ms();
            // 电量缓存:唯一的 I2C 读取点(30s 一次),UI 只读缓存。
            if (now0 - last_soc_ms >= 30000) {
                app_runtime()->battery_soc = bsp_battery_soc();
                last_soc_ms = now0;
            }
            if (now0 - last_beat_ms >= 10000) {
                last_beat_ms = now0;
                app_debug_heartbeat();   // 统一观测点:见 app_debug.c
            }
        }
        app_runtime_t *rt = app_runtime();
        int64_t now = now_ms();
        int64_t idle_ms = now - s_last_activity_ms;
        uint32_t auto_off_ms = (uint32_t)rt->settings.auto_off_s * 1000U;

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
            int64_t pre_sleep = now_ms();
            if (usb_host_attached()) {
                // 主机在位:浅睡会引来 USB 总线复位腰斩整机(见探针处注释),
                // 退化为分段延时;循环顶部的 500ms 节拍继续驱动心跳与到点判断。
                static bool logged_usb;
                if (!logged_usb) {
                    ESP_LOGW(TAG, "USB 主机在位,计时浅睡退化为延时(防总线复位)");
                    logged_usb = true;
                }
                vTaskDelay(pdMS_TO_TICKS(slice > 1000 ? 1000 : slice));
            } else {
                light_sleep_slice(slice);
            }
            app_service_tick(now_ms());
            // 时钟连续性观测点:墙钟若不随浅睡推进,单元边界永远不会到。
            ESP_LOGI(TAG, "熄屏计时片: 请求%lldms 墙钟走%lldms 下边界还%lldms",
                     (long long)slice, (long long)(now_ms() - pre_sleep),
                     (long long)app_focus_next_boundary_ms(&rt->focus, now_ms()));
            if (!rt->focus.running) app_power_wake_screen();   // 胜利:亮屏播动画
            continue;
        }
        if (now - s_screen_off_at_ms >= DEEP_SLEEP_GRACE_MS &&
            !rt->sync_in_progress) {
            if (usb_host_attached()) {
                // 主机在位:深睡唤醒的开机窗口同样会撞上 USB 总线复位,
                // 可能撕断开机早期的 NVS 写入(素材版本记账)。USB 供电下
                // 保持清醒即可;拔线后的空闲周期照常深睡。
                static bool logged_ds;
                if (!logged_ds) {
                    ESP_LOGW(TAG, "USB 主机在位,暂不深睡");
                    logged_ds = true;
                }
                continue;
            }
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

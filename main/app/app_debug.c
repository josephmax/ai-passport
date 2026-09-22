// main/app/app_debug.c —— 观测点实现。
#include "app_debug.h"

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "dbg";

#define DEBUG_TASKS_MAX 8
static struct {
    const char *name;
    TaskHandle_t handle;
} s_tasks[DEBUG_TASKS_MAX];
static int s_task_count;

// RTC 快存:软复位/崩溃重启/深睡唤醒均保留(断电清零,另有 magic 判别)。
static RTC_DATA_ATTR uint32_t s_run_magic;
static RTC_DATA_ATTR uint32_t s_run_seconds;
#define RUN_MAGIC 0x52554E31   // "RUN1"

void app_debug_register(const char *name, void *task_handle) {
    if (s_task_count >= DEBUG_TASKS_MAX) return;
    s_tasks[s_task_count].name = name;
    s_tasks[s_task_count].handle = (TaskHandle_t)task_handle;
    s_task_count++;
}

void app_debug_boot_report(void) {
    esp_reset_reason_t r = esp_reset_reason();
    const char *why = "未知";
    switch (r) {
    case ESP_RST_POWERON: why = "上电"; break;
    case ESP_RST_SW: why = "软件复位"; break;
    case ESP_RST_PANIC: why = "崩溃(panic)重启"; break;
    case ESP_RST_INT_WDT: why = "中断看门狗"; break;
    case ESP_RST_TASK_WDT: why = "任务看门狗"; break;
    case ESP_RST_WDT: why = "其他看门狗"; break;
    case ESP_RST_BROWNOUT: why = "欠压"; break;
    case ESP_RST_DEEPSLEEP: why = "深睡唤醒"; break;
    case ESP_RST_SDIO: why = "SDIO"; break;
    default: break;
    }
    if (s_run_magic == RUN_MAGIC) {
        ESP_LOGW(TAG, "开机诊断: 复位原因=%s 上次运行=%us (软复位前存活时长)",
                 why, (unsigned)s_run_seconds);
    } else {
        ESP_LOGI(TAG, "开机诊断: 复位原因=%s (断电后首启,无上次记录)", why);
    }
    s_run_magic = RUN_MAGIC;
    s_run_seconds = 0;
}

bool app_debug_heartbeat(void) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "心跳 运行=%us 堆=%u LVGL池空闲=%u(最大块%u,用量%d%%,碎片%d%%)",
             (unsigned)s_run_seconds,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)mon.free_size,
             (unsigned)mon.free_biggest_size,
             mon.used_pct,
             mon.frag_pct);
    for (int i = 0; i < s_task_count; i++) {
        if (!s_tasks[i].handle) continue;
        UBaseType_t free_words = uxTaskGetStackHighWaterMark(s_tasks[i].handle);
        ESP_LOGI(TAG, "  栈[%s] 剩余=%u 字", s_tasks[i].name,
                 (unsigned)(free_words * sizeof(StackType_t)));
    }
    s_run_seconds += 10;
    return true;
}

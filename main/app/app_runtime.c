// main/app/app_runtime.c —— 运行时状态单例与事件基。
#include "app_runtime.h"

#include <string.h>

ESP_EVENT_DEFINE_BASE(APP_EVENT);

static app_runtime_t s_rt;

// 快照双缓冲(契约见 app_runtime.h)。指针本身 volatile:发布即翻转,
// 写方对缓冲内容的整份拷贝完成于翻转之前。
static app_snapshot_t s_snap_buf[2];
static app_snapshot_t *volatile s_snap_active = &s_snap_buf[0];

void app_runtime_set_snapshot(const app_snapshot_t *snap) {
    app_snapshot_t *next =
        s_snap_active == &s_snap_buf[0] ? &s_snap_buf[1] : &s_snap_buf[0];
    *next = *snap;
    s_snap_active = next;
}

const app_snapshot_t *app_runtime_snap(void) {
    return s_snap_active;
}

app_runtime_t *app_runtime(void) {
    return &s_rt;
}

void app_runtime_publish(app_event_id_t id) {
    esp_event_post(APP_EVENT, (int32_t)id, NULL, 0, 0);
}

// main/app/app_runtime.c —— 运行时状态单例与事件基。
#include "app_runtime.h"

#include <string.h>

ESP_EVENT_DEFINE_BASE(APP_EVENT);

static app_runtime_t s_rt;

app_runtime_t *app_runtime(void) {
    return &s_rt;
}

void app_runtime_publish(app_event_id_t id) {
    esp_event_post(APP_EVENT, (int32_t)id, NULL, 0, 0);
}

#include "app_ble_startup.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"

static const char *TAG = "ble_start";

static void memory_log(const char *stage) {
    ESP_LOGI(TAG, "%s: internal free=%u largest=%u", stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

static void worker_gate(void *arg) {
    app_ble_startup_t *state = arg;
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    state->config.worker(NULL);
    vTaskDelete(NULL);
}

static void host_gate(void *arg) {
    app_ble_startup_t *state = arg;
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    state->config.host(NULL);
    vTaskDelete(NULL);
}

static bool rollback(app_ble_startup_t *state) {
    /* Neither task has passed its gate on a failed startup. There is no running
     * host to stop, and no worker can race the queue/stack teardown. */
    if (state->host) { vTaskDelete(state->host); state->host = NULL; }
    if (state->worker) { vTaskDelete(state->worker); state->worker = NULL; }
    if (state->stack_live) {
        esp_err_t err = nimble_port_deinit();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "stack rollback failed: %s", esp_err_to_name(err));
            return false;
        }
        state->stack_live = false;
    }
    if (state->queue) { vQueueDelete(state->queue); state->queue = NULL; }
    return true;
}

bool app_ble_startup(app_ble_startup_t *state,
                     const app_ble_startup_config_t *config) {
    if (state->ready) return true;
    if (!rollback(state)) return false;
    state->config = *config;
    state->failed_stage = "queue";
    int err = ESP_ERR_NO_MEM;
    memory_log("before task reservation");
    state->queue = xQueueCreate(8, config->message_size);
    if (!state->queue) goto fail;
    state->failed_stage = "worker task";
    if (xTaskCreate(worker_gate, "ble_app", 6144, state, 4, &state->worker) != pdPASS)
        goto fail;
    state->failed_stage = "host task";
    if (xTaskCreate(host_gate, "nimble_host", CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,
                    state, configMAX_PRIORITIES - 4, &state->host) != pdPASS)
        goto fail;
    memory_log("tasks reserved");
    /* Cold controller/host initialization and the first LVGL frame must not
     * keep a higher-priority task runnable across the entire IDLE WDT window.
     * Allow normal scheduling between bounded initialization stages. */
    vTaskDelay(pdMS_TO_TICKS(10));
    state->failed_stage = "NimBLE stack";
    err = nimble_port_init();
    if (err != ESP_OK) goto fail;
    state->stack_live = true;
    memory_log("stack initialized");
    vTaskDelay(pdMS_TO_TICKS(10));
    state->failed_stage = "GATT services";
    err = config->configure();
    if (err) goto fail;
    vTaskDelay(pdMS_TO_TICKS(10));
    state->failed_stage = NULL;
    state->ready = true;
    xTaskNotifyGive(state->worker);
    xTaskNotifyGive(state->host);
    memory_log("ready");
    return true;

fail:
    ESP_LOGE(TAG, "startup failed at %s (code=%d)", state->failed_stage, err);
    memory_log("failure");
    (void)rollback(state);
    memory_log("after rollback");
    return false;
}

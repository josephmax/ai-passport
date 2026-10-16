#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* Both task entries remain blocked until the stack and GATT configuration are
 * complete. Failed startup releases everything it created before allowing retry. */
typedef struct {
    size_t message_size;
    TaskFunction_t worker;
    TaskFunction_t host;
    int (*configure)(void);
} app_ble_startup_config_t;

typedef struct {
    QueueHandle_t queue;
    TaskHandle_t worker, host;
    bool stack_live, ready;
    const char *failed_stage;
    app_ble_startup_config_t config;
} app_ble_startup_t;

bool app_ble_startup(app_ble_startup_t *state,
                     const app_ble_startup_config_t *config);

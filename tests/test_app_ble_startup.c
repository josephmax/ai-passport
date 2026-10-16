#include "app/app_ble_startup.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

struct test_task { TaskFunction_t entry; void *arg; bool live, released; size_t bytes; };
struct test_queue { bool live; size_t bytes; };
static struct test_task tasks[2], *current;
static struct test_queue queue;
static jmp_buf blocked;
static size_t used;
static int failure, allocations, stack_inits, stack_deinits, task_calls;
static int scheduling_breaks;
static bool stack_live, configured, fail_deinit;

const char *esp_err_to_name(esp_err_t err) { (void)err; return "injected error"; }
void test_startup_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 100000 - used; }
size_t heap_caps_get_largest_free_block(unsigned caps) { return heap_caps_get_free_size(caps); }
static bool allocate(size_t bytes) {
    if (++allocations == failure) return false;
    used += bytes; return true;
}
QueueHandle_t xQueueCreate(unsigned count, unsigned size) {
    assert(!queue.live && count == 8 && size == 268);
    if (!allocate(count * size)) return NULL;
    queue = (struct test_queue){true, count * size}; return &queue;
}
void vQueueDelete(QueueHandle_t handle) {
    assert(handle == &queue && queue.live && !stack_live);
    used -= queue.bytes; queue.live = false;
}
BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, unsigned bytes,
                       void *arg, unsigned priority, TaskHandle_t *out) {
    int index = !strcmp(name, "ble_app") ? 0 : 1;
    assert(!stack_live && !tasks[index].live);
    assert(bytes == (index ? 4096 : 6144));
    assert(priority == (index ? configMAX_PRIORITIES - 4 : 4));
    if (!allocate(bytes)) return 0;
    tasks[index] = (struct test_task){entry, arg, true, false, bytes};
    *out = &tasks[index]; current = *out;
    /* Simulate a higher-priority task running immediately upon creation. It
     * must block before touching NimBLE or application state. */
    if (!setjmp(blocked)) { entry(arg); assert(false); }
    current = NULL; return pdPASS;
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait) {
    assert(clear == pdTRUE && wait == portMAX_DELAY && current);
    if (!current->released) longjmp(blocked, 1);
    assert(configured); return 1;
}
void vTaskDelete(TaskHandle_t task) {
    if (!task) { assert(current && current->released); return; }
    assert(task->live && !task->released);
    used -= task->bytes; task->live = false;
}
void vTaskDelay(TickType_t ticks) {
    assert(ticks == pdMS_TO_TICKS(10));
    assert(tasks[0].live && tasks[1].live && !tasks[0].released && !tasks[1].released);
    ++scheduling_breaks;
}
void xTaskNotifyGive(TaskHandle_t task) {
    assert(task->live && !task->released && configured && stack_live);
    app_ble_startup_t *state = task->arg;
    assert(state->ready);
    task->released = true; current = task;
    task->entry(task->arg); current = NULL;
}
esp_err_t nimble_port_init(void) {
    ++stack_inits;
    assert(tasks[0].live && tasks[1].live && !tasks[0].released && !tasks[1].released);
    assert(!stack_live);
    if (!allocate(60000)) return ESP_ERR_NO_MEM;
    stack_live = true; return ESP_OK;
}
esp_err_t nimble_port_deinit(void) {
    ++stack_deinits;
    assert(stack_live && !tasks[0].live && !tasks[1].live);
    if (fail_deinit) return ESP_ERR_NO_MEM;
    used -= 60000; stack_live = false; configured = false; return ESP_OK;
}
static int configure(void) {
    assert(stack_live && !tasks[0].released && !tasks[1].released);
    if (++allocations == failure) return 6;
    configured = true; return 0;
}
static void task_entry(void *arg) { assert(!arg && configured); ++task_calls; }
static void reset(void) {
    memset(tasks, 0, sizeof(tasks)); memset(&queue, 0, sizeof(queue));
    used = 0; failure = allocations = stack_inits = stack_deinits = task_calls = 0;
    scheduling_breaks = 0;
    stack_live = configured = fail_deinit = false;
}
int main(void) {
    const app_ble_startup_config_t config = {268, task_entry, task_entry, configure};
    for (int stage = 1; stage <= 5; ++stage) {
        reset(); failure = stage;
        app_ble_startup_t state = {0};
        assert(!app_ble_startup(&state, &config));
        assert(state.failed_stage && !state.ready && !state.queue);
        assert(!state.worker && !state.host && !state.stack_live && !used && !task_calls);
        assert(stack_deinits == (stage == 5));
        /* A failed call is not cached as success: retry creates all resources. */
        failure = 0;
        scheduling_breaks = 0;
        assert(app_ble_startup(&state, &config));
        assert(state.ready && !state.failed_stage && task_calls == 2);
        assert(scheduling_breaks == 3);
        int before = allocations;
        assert(app_ble_startup(&state, &config));
        assert(before == allocations && task_calls == 2);
    }
    reset(); failure = 5; fail_deinit = true;
    app_ble_startup_t state = {0};
    assert(!app_ble_startup(&state, &config));
    assert(state.stack_live && !state.ready && !state.worker && !state.host && state.queue);
    int before = stack_inits;
    assert(!app_ble_startup(&state, &config));
    assert(stack_inits == before); // No second controller over a live failed cleanup.
    fail_deinit = false; failure = 0;
    assert(app_ble_startup(&state, &config));
    assert(state.ready && task_calls == 2 && stack_inits == before + 1);
    puts("BLE startup allocation, task gates and rollback tests: PASS");
}

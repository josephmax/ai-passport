#include "ui/ui_event_batch.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    uint32_t pending = 0;
    assert(ui_event_batch_take(&pending) == 0);
    for (int i = 0; i < 1000; ++i) ui_event_batch_post(&pending, UI_EVENT_REFRESH);
    ui_event_batch_post(&pending, UI_EVENT_VICTORY);
    ui_event_batch_post(&pending, UI_EVENT_SCREEN_OFF);
    ui_event_batch_post(&pending, UI_EVENT_REFRESH);
    ui_event_batch_post(&pending, UI_EVENT_SCREEN_ON);
    uint32_t events = ui_event_batch_take(&pending);
    assert(events == (UI_EVENT_REFRESH | UI_EVENT_VICTORY | UI_EVENT_SCREEN_ON));
    assert(!pending); // A full refresh queue never loses victory/wake events.
    // A callback arriving while the previous batch is rendered is next batch.
    ui_event_batch_post(&pending, UI_EVENT_SCREEN_OFF);
    ui_event_batch_post(&pending, UI_EVENT_REFRESH);
    assert(events & UI_EVENT_SCREEN_ON);
    assert(ui_event_batch_take(&pending) == (UI_EVENT_SCREEN_OFF | UI_EVENT_REFRESH));
    for (int i = 0; i < 1000; ++i) {
        ui_event_batch_post(&pending, UI_EVENT_SCREEN_ON);
        ui_event_batch_post(&pending, UI_EVENT_SCREEN_OFF);
        assert(ui_event_batch_take(&pending) == UI_EVENT_SCREEN_OFF);
        ui_event_batch_post(&pending, UI_EVENT_SCREEN_OFF);
        ui_event_batch_post(&pending, UI_EVENT_SCREEN_ON);
        assert(ui_event_batch_take(&pending) == UI_EVENT_SCREEN_ON);
    }
    puts("UI event coalescing, last-screen order and deferred batch tests: PASS");
}

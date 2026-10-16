#include "ui_event_batch.h"

void ui_event_batch_post(uint32_t *pending, uint32_t event) {
    if (event == UI_EVENT_SCREEN_OFF || event == UI_EVENT_SCREEN_ON)
        *pending &= ~(UI_EVENT_SCREEN_OFF | UI_EVENT_SCREEN_ON);
    *pending |= event;
}

uint32_t ui_event_batch_take(uint32_t *pending) {
    uint32_t events = *pending;
    *pending = 0;
    return events;
}

#pragma once
#include <stdint.h>

enum {
    UI_EVENT_REFRESH = 1u << 0,
    UI_EVENT_VICTORY = 1u << 1,
    UI_EVENT_SCREEN_OFF = 1u << 2,
    UI_EVENT_SCREEN_ON = 1u << 3,
};

/* Coalesce refreshes but retain victory and the last screen transition.
 * Caller serializes post/take; neither operation renders or allocates. */
void ui_event_batch_post(uint32_t *pending, uint32_t event);
uint32_t ui_event_batch_take(uint32_t *pending);

// main/app/app_focus.c —— 番茄计时状态机实现。纯 C11,无平台依赖。
#include "app_focus.h"

#include <stddef.h>

void app_focus_reset(app_focus_state_t *st) {
    st->running = false;
    st->units = 0;
    st->xp_granted = 0;
    st->end_at_ms = 0;
}

app_focus_ev_t app_focus_start(app_focus_state_t *st, int64_t now_ms) {
    if (st->running) return APP_FOCUS_EV_NONE;
    st->running = true;
    st->units = 1;
    st->xp_granted = 0;
    st->end_at_ms = now_ms + APP_FOCUS_UNIT_MS;
    return APP_FOCUS_EV_STARTED;
}

app_focus_ev_t app_focus_add_unit(app_focus_state_t *st, int64_t now_ms) {
    if (!st->running) return app_focus_start(st, now_ms);
    if (st->units >= APP_FOCUS_MAX_UNITS) return APP_FOCUS_EV_STACK_FULL;
    st->units++;
    st->end_at_ms += APP_FOCUS_UNIT_MS;
    return APP_FOCUS_EV_UNIT_ADDED;
}

app_focus_ev_t app_focus_cancel(app_focus_state_t *st) {
    if (!st->running) return APP_FOCUS_EV_NONE;
    app_focus_reset(st);
    return APP_FOCUS_EV_CANCELLED;
}

app_focus_ev_t app_focus_poll(app_focus_state_t *st, int64_t now_ms) {
    if (!st->running) return APP_FOCUS_EV_NONE;

    // 已越过的边界数(含排空瞬间)。remaining 用向上取整:边界未到就不算。
    int64_t remaining_ms = st->end_at_ms - now_ms;
    int remaining_units = (int)((remaining_ms + APP_FOCUS_UNIT_MS - 1) / APP_FOCUS_UNIT_MS);
    if (remaining_units < 0) remaining_units = 0;
    if (remaining_units > st->units) remaining_units = st->units;   // 时钟回拨防御

    uint8_t elapsed = st->units - (uint8_t)remaining_units;
    if ((uint8_t)elapsed > st->xp_granted) {
        // 先补齐静默经验,一次一个事件,调用方逐个 +1 XP。
        st->xp_granted++;
        return APP_FOCUS_EV_UNIT_DONE;
    }
    if (remaining_units == 0) {
        st->running = false;
        return APP_FOCUS_EV_VICTORY;
    }
    return APP_FOCUS_EV_NONE;
}

int64_t app_focus_remaining_ms(const app_focus_state_t *st, int64_t now_ms) {
    if (!st->running) return 0;
    int64_t left = st->end_at_ms - now_ms;
    return left > 0 ? left : 0;
}

int64_t app_focus_next_boundary_ms(const app_focus_state_t *st, int64_t now_ms) {
    if (!st->running) return -1;
    int64_t left = st->end_at_ms - now_ms;
    if (left <= 0) return 0;
    // 边界发生在 end_at - k*UNIT(k=剩余单元数-1..0)。最近一个:
    int64_t remaining_units = (left + APP_FOCUS_UNIT_MS - 1) / APP_FOCUS_UNIT_MS;
    return left - (remaining_units - 1) * APP_FOCUS_UNIT_MS;
}

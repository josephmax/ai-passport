// tests/test_app_focus.c —— 番茄计时状态机主机单测(规格 §6.1/§12)。
#include "app/app_focus.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define UNIT_MS APP_FOCUS_UNIT_MS

static void test_start_and_add(void) {
    app_focus_state_t st;
    app_focus_reset(&st);
    int64_t now = 1000000;

    assert(app_focus_start(&st, now) == APP_FOCUS_EV_STARTED);
    assert(st.running && st.units == 1 && st.xp_granted == 0);
    assert(st.end_at_ms == now + UNIT_MS);
    assert(app_focus_remaining_ms(&st, now) == UNIT_MS);

    assert(app_focus_add_unit(&st, now) == APP_FOCUS_EV_UNIT_ADDED);
    assert(st.units == 2 && st.end_at_ms == now + 2 * UNIT_MS);

    // 堆到 5 个后再按:STACK_FULL,状态不变
    for (int i = 3; i <= APP_FOCUS_MAX_UNITS; i++) {
        assert(app_focus_add_unit(&st, now) == APP_FOCUS_EV_UNIT_ADDED);
    }
    assert(st.units == APP_FOCUS_MAX_UNITS);
    int64_t end_before = st.end_at_ms;
    assert(app_focus_add_unit(&st, now) == APP_FOCUS_EV_STACK_FULL);
    assert(st.units == APP_FOCUS_MAX_UNITS && st.end_at_ms == end_before);
    printf("start_and_add ok\n");
}

static void test_poll_boundaries_and_victory(void) {
    app_focus_state_t st;
    app_focus_reset(&st);
    int64_t now = 5000;
    assert(app_focus_start(&st, now) == APP_FOCUS_EV_STARTED);
    assert(app_focus_add_unit(&st, now) == APP_FOCUS_EV_UNIT_ADDED);   // 2 单元

    assert(app_focus_poll(&st, now + UNIT_MS - 1) == APP_FOCUS_EV_NONE);
    // 第一个单元边界:静默 +1 经验
    assert(app_focus_poll(&st, now + UNIT_MS) == APP_FOCUS_EV_UNIT_DONE);
    assert(st.xp_granted == 1 && st.running && st.units == 2);
    assert(app_focus_poll(&st, now + UNIT_MS + 5) == APP_FOCUS_EV_NONE);
    // 第二个单元排空:先 UNIT_DONE 再 VICTORY
    assert(app_focus_poll(&st, now + 2 * UNIT_MS) == APP_FOCUS_EV_UNIT_DONE);
    assert(app_focus_poll(&st, now + 2 * UNIT_MS) == APP_FOCUS_EV_VICTORY);
    assert(!st.running);
    assert(app_focus_poll(&st, now + 3 * UNIT_MS) == APP_FOCUS_EV_NONE);
    printf("poll_boundaries_and_victory ok\n");
}

static void test_cancel_grants_nothing(void) {
    app_focus_state_t st;
    app_focus_reset(&st);
    int64_t now = 0;
    app_focus_start(&st, now);
    // 第一个单元已完成(经验已授),取消只丢未完成的
    assert(app_focus_poll(&st, now + UNIT_MS) == APP_FOCUS_EV_UNIT_DONE);
    assert(app_focus_cancel(&st) == APP_FOCUS_EV_CANCELLED);
    assert(!st.running && st.units == 0 && st.xp_granted == 0);
    assert(app_focus_remaining_ms(&st, now + UNIT_MS) == 0);
    printf("cancel ok\n");
}

static void test_power_loss_recompute(void) {
    // 掉电重启:按绝对时间重算 —— 状态由 NVS 恢复,直接 poll 循环补齐。
    app_focus_state_t st;
    app_focus_reset(&st);
    int64_t now = 777;
    app_focus_start(&st, now);
    for (int i = 2; i <= 3; i++) app_focus_add_unit(&st, now);
    assert(st.units == 3);

    // 重启时已经过 2 个单元 + 10 分钟
    int64_t boot = now + 2 * UNIT_MS + 10 * 60 * 1000;
    int ev_cnt[8] = {0};
    app_focus_ev_t ev;
    int guard = 0;
    while ((ev = app_focus_poll(&st, boot)) != APP_FOCUS_EV_NONE) {
        assert(guard++ < 10);
        ev_cnt[ev]++;
    }
    assert(ev_cnt[APP_FOCUS_EV_UNIT_DONE] == 2);
    assert(st.running && st.xp_granted == 2);
    assert(app_focus_remaining_ms(&st, boot) == UNIT_MS - 10 * 60 * 1000);

    // 全部结束发生在断电期间:补齐经验并吐胜利
    int64_t boot2 = now + 3 * UNIT_MS + 999;
    while ((ev = app_focus_poll(&st, boot2)) != APP_FOCUS_EV_NONE) {
        ev_cnt[ev]++;
    }
    assert(ev_cnt[APP_FOCUS_EV_UNIT_DONE] == 3);
    assert(ev_cnt[APP_FOCUS_EV_VICTORY] == 1);
    assert(!st.running);
    printf("power_loss_recompute ok\n");
}

static void test_next_boundary(void) {
    app_focus_state_t st;
    app_focus_reset(&st);
    assert(app_focus_next_boundary_ms(&st, 0) == -1);
    int64_t now = 42;
    app_focus_start(&st, now);
    assert(app_focus_next_boundary_ms(&st, now) == UNIT_MS);
    assert(app_focus_next_boundary_ms(&st, now + UNIT_MS - 1000) == 1000);
    app_focus_add_unit(&st, now);
    // 2 单元共 50min:5min 处剩余 45min → 下一边界在 45-25=20min 后
    assert(app_focus_next_boundary_ms(&st, now + 5 * 60 * 1000) == 20 * 60 * 1000);
    assert(app_focus_next_boundary_ms(&st, now + 2 * UNIT_MS) == 0);
    printf("next_boundary ok\n");
}

int main(void) {
    test_start_and_add();
    test_poll_boundaries_and_victory();
    test_cancel_grants_nothing();
    test_power_loss_recompute();
    test_next_boundary();
    printf("test_app_focus: all passed\n");
    return 0;
}

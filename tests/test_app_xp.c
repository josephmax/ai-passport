// tests/test_app_xp.c —— 经验等级分段表 + 今日/本周回零主机单测(规格 §6.2)。
#include "app/app_time.h"
#include "app/app_xp.h"

#include <assert.h>
#include <stdio.h>

static void test_segment_table_totals(void) {
    // 分段累计:到 10 级 45,30 级 345,60 级 1095,99 级 3045,100 级 3145
    assert(app_xp_level_base(1) == 0);
    assert(app_xp_level_base(10) == 9 * 5);
    assert(app_xp_level_base(30) == 9 * 5 + 20 * 15);
    assert(app_xp_level_base(60) == 9 * 5 + 20 * 15 + 30 * 25);
    assert(app_xp_level_base(99) == 9 * 5 + 20 * 15 + 30 * 25 + 39 * 50);
    assert(app_xp_level_base(100) == APP_XP_TOTAL_MAX);
    assert(APP_XP_TOTAL_MAX == 3145);
    printf("segment_table_totals ok\n");
}

static void test_level_lookup_boundaries(void) {
    assert(app_xp_level(0) == 1);
    assert(app_xp_level(4) == 1);
    assert(app_xp_level(5) == 2);
    assert(app_xp_level(44) == 9);
    assert(app_xp_level(45) == 10);
    assert(app_xp_level(59) == 10);
    assert(app_xp_level(60) == 11);
    assert(app_xp_level(344) == 29);
    assert(app_xp_level(345) == 30);
    assert(app_xp_level(1094) == 59);
    assert(app_xp_level(1095) == 60);
    assert(app_xp_level(3044) == 98);
    assert(app_xp_level(3045) == 99);
    assert(app_xp_level(3144) == 99);
    assert(app_xp_level(3145) == 100);
    assert(app_xp_level(999999) == 100);   // 越界收敛
    assert(app_xp_level(-5) == 1);
    printf("level_lookup_boundaries ok\n");
}

static void test_to_next_and_permille(void) {
    assert(app_xp_to_next(0) == 5);
    assert(app_xp_to_next(44) == 1);
    assert(app_xp_to_next(45) == 15);
    assert(app_xp_to_next(3045) == 100);
    assert(app_xp_to_next(3145) == 0);      // 满级

    assert(app_xp_level_permille(0) == 0);
    assert(app_xp_level_permille(2) == 400);   // 2/5
    assert(app_xp_level_permille(5) == 0);     // 刚升级
    assert(app_xp_level_permille(3145) == 1000);
    printf("to_next_and_permille ok\n");
}

static void test_store_rollover(void) {
    app_xp_store_t st;
    app_xp_store_reset(&st);
    // 2026-09-21(周一)与 22(周二)的本地日索引;用 app_time 的公式互检。
    int64_t mon = (int64_t)1770000000;   // 任取,只要求彼此关系正确
    uint32_t d1 = app_time_day_index(mon);
    uint32_t w1 = app_time_week_index(mon);

    st.last_day = d1;
    st.last_week = w1;
    st.total_xp = 10;
    st.today_count = 3;
    st.week_count = 6;
    assert(!app_xp_rollover(&st, d1, w1));
    app_xp_add_unit(&st);
    assert(st.total_xp == 11 && st.today_count == 4 && st.week_count == 7);

    // 同周跨日:today 清零,week 保留
    assert(app_xp_rollover(&st, d1 + 1, w1));
    assert(st.today_count == 0 && st.week_count == 7 && st.total_xp == 11);
    app_xp_add_unit(&st);
    assert(st.today_count == 1 && st.week_count == 8);

    // 跨周:两个都清零
    assert(app_xp_rollover(&st, d1 + 7, w1 + 1));
    assert(st.today_count == 0 && st.week_count == 0);
    printf("store_rollover ok\n");
}

static void test_week_anchor_is_monday(void) {
    // 1970-01-01 是周四:day 0..3 属周 0,day 4(周一)起属周 1。
    assert(app_time_week_index(0) == 0);
    assert(app_time_week_index(3 * 86400 + 86399) == 0);
    assert(app_time_week_index(4 * 86400) == 1);
    assert(app_time_week_index(4 * 86400 - 1) == 0);
    // 本地 0 点即日界:负数不出现(时钟校准只向 1970 后走)
    assert(app_time_min_of_day(86399) == 23 * 60 + 59);
    assert(app_time_day_index(86400) == 1);
    assert(app_time_same_local_day(APP_TIME_PLAUSIBLE_S + 100,
                                   APP_TIME_PLAUSIBLE_S + 200));
    assert(!app_time_same_local_day(APP_TIME_PLAUSIBLE_S - 1,
                                    APP_TIME_PLAUSIBLE_S + 200));
    assert(!app_time_same_local_day(1727567999LL, 1727568000LL));
    printf("week_anchor ok\n");
}

static void test_rest_window(void) {
    app_rest_window_t w = { 23 * 60, 8 * 60 };   // 默认 23:00–次日 8:00
    assert(!app_time_in_rest(22 * 60, w));
    assert(app_time_in_rest(23 * 60, w));
    assert(app_time_in_rest(0, w));
    assert(app_time_in_rest(7 * 60 + 59, w));
    assert(!app_time_in_rest(8 * 60, w));
    app_rest_window_t none = { 600, 600 };
    assert(!app_time_in_rest(600, none));   // 起止相等=禁用
    app_rest_window_t day = { 9 * 60, 12 * 60 };
    assert(app_time_in_rest(9 * 60, day) && !app_time_in_rest(12 * 60, day));
    printf("rest_window ok\n");
}

int main(void) {
    test_segment_table_totals();
    test_level_lookup_boundaries();
    test_to_next_and_permille();
    test_store_rollover();
    test_week_anchor_is_monday();
    test_rest_window();
    printf("test_app_xp: all passed\n");
    return 0;
}

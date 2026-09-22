// main/app/app_xp.c —— 经验与等级实现。纯 C11。
#include "app_xp.h"

#include <stddef.h>

// 分段定值表:{起始等级, 升到下一级所需番茄}。行 i 覆盖 [start, next_start)。
static const struct {
    int start_level;
    int cost;
} SEG[] = {
    { 1, 5 },     // 1–9 级:每级 5
    { 10, 15 },   // 10–29 级:每级 15
    { 30, 25 },   // 30–59 级:每级 25
    { 60, 50 },   // 60–98 级:每级 50
    { 99, 100 },  // 99 -> 100(终局)
};
#define SEG_COUNT (sizeof(SEG) / sizeof(SEG[0]))

static int clamp_total(int total_xp) {
    if (total_xp < 0) return 0;
    if (total_xp > APP_XP_TOTAL_MAX) return APP_XP_TOTAL_MAX;
    return total_xp;
}

int app_xp_level_base(int level) {
    // 累加到达 level 前所有分段的完整级别开销。
    if (level <= 1) return 0;
    if (level > APP_XP_MAX_LEVEL) level = APP_XP_MAX_LEVEL;
    int base = 0;
    for (size_t i = 0; i < SEG_COUNT; i++) {
        int start = SEG[i].start_level;
        int end = (i + 1 < SEG_COUNT) ? SEG[i + 1].start_level : APP_XP_MAX_LEVEL;
        if (level > start) {
            int span = (level < end ? level : end) - start;
            base += span * SEG[i].cost;
        }
    }
    return base;
}

int app_xp_level(int total_xp) {
    total_xp = clamp_total(total_xp);
    for (size_t i = 0; i < SEG_COUNT; i++) {
        int start = SEG[i].start_level;
        int end = (i + 1 < SEG_COUNT) ? SEG[i + 1].start_level : APP_XP_MAX_LEVEL;
        int seg_base = app_xp_level_base(start);
        int span = end - start;
        if (total_xp < seg_base + span * SEG[i].cost) {
            // 在本段内:起点 + 向下取整。恰好等于段终点的值落不到这里,循环外返回满级。
            return start + (total_xp - seg_base) / SEG[i].cost;
        }
    }
    return APP_XP_MAX_LEVEL;
}

int app_xp_to_next(int total_xp) {
    total_xp = clamp_total(total_xp);
    int level = app_xp_level(total_xp);
    if (level >= APP_XP_MAX_LEVEL) return 0;
    return app_xp_level_base(level + 1) - total_xp;
}

int app_xp_level_permille(int total_xp) {
    total_xp = clamp_total(total_xp);
    int level = app_xp_level(total_xp);
    if (level >= APP_XP_MAX_LEVEL) return 1000;
    int base = app_xp_level_base(level);
    int cost = app_xp_to_next(base);   // 本级升级开销
    if (cost <= 0) return 1000;
    int done = total_xp - base;
    int permille = done * 1000 / cost;
    if (permille > 1000) permille = 1000;
    return permille;
}

void app_xp_store_reset(app_xp_store_t *st) {
    st->total_xp = 0;
    st->today_count = 0;
    st->week_count = 0;
    st->last_day = 0;
    st->last_week = 0;
}

bool app_xp_rollover(app_xp_store_t *st, uint32_t day_idx, uint32_t week_idx) {
    bool changed = false;
    if (st->last_day != day_idx) {
        st->today_count = 0;
        st->last_day = day_idx;
        changed = true;
    }
    if (st->last_week != week_idx) {
        st->week_count = 0;
        st->last_week = week_idx;
        changed = true;
    }
    return changed;
}

void app_xp_add_unit(app_xp_store_t *st) {
    if (st->total_xp < APP_XP_TOTAL_MAX) st->total_xp++;
    if (st->today_count < 0xFFFF) st->today_count++;
    if (st->week_count < 0xFFFF) st->week_count++;
}

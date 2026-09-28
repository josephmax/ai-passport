// main/app/app_time.h —— 时间辅助(纯逻辑)。
//
// 设备系统时钟约定走【本地时间】:对时来源(快照 generatedAt)自带时区偏移,
// 校准时直接把偏移折算进去。于是本地 0 点 = 86400 对齐、作息/今日/本周都
// 是纯模运算。中国无夏令时,该约定在规格风险 #5 中已接受。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Treat the clock as untrusted before 2020-01-01.
#define APP_TIME_PLAUSIBLE_S 1577836800LL

// 本地日索引(自 1970-01-01 起的天数)。历元第 0 天是周四。
static inline uint32_t app_time_day_index(int64_t local_epoch_s) {
    return (uint32_t)(local_epoch_s / 86400);
}

// Today-only readings cannot be reused after midnight or before clock sync.
static inline bool app_time_same_local_day(int64_t sample_s, int64_t now_s) {
    return sample_s >= APP_TIME_PLAUSIBLE_S && now_s >= APP_TIME_PLAUSIBLE_S &&
           app_time_day_index(sample_s) == app_time_day_index(now_s);
}

// 周一锚点的周索引:周一 0 点跳变。day 0(周四)记 0,首个周一是 day 4 → 周 1。
static inline uint32_t app_time_week_index(int64_t local_epoch_s) {
    return (uint32_t)((local_epoch_s / 86400 + 3) / 7);
}

// 一天内的分钟数 0..1439。
static inline int app_time_min_of_day(int64_t local_epoch_s) {
    return (int)((local_epoch_s % 86400) / 60);
}

// 时钟视为可信的最小历元秒(2020-01-01)。低于此值不跑日界/时段逻辑。

// 作息窗口(规格 §5.4,默认 23:00–次日 8:00,设置页 ±30 分钟步进)。
typedef struct {
    uint16_t start_min;   // 起始分钟(含)
    uint16_t end_min;     // 结束分钟(不含)
} app_rest_window_t;

#define APP_REST_DEFAULT { 23 * 60, 8 * 60 }

// start==end 视为禁用(全天不睡);start<end 为当日窗口;start>end 跨午夜。
bool app_time_in_rest(int min_of_day, app_rest_window_t window);

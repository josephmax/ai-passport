// main/app/app_fmt.c —— 展示格式化实现。纯 C11。
#include "app_fmt.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

void app_fmt_daily_tokens(double value, char *buf, size_t cap) {
    if (!isfinite(value) || value < 0) { snprintf(buf, cap, "--"); return; }
    if (value >= 1e16) { snprintf(buf, cap, "9999T"); return; }
    uint64_t n = (uint64_t)value;
    if (n < 1000) { snprintf(buf, cap, "%" PRIu64, n); return; }
    uint64_t unit = 1000;
    int suffix = 0;
    while (suffix < 3 && n >= unit * 1000) { unit *= 1000; suffix++; }
    uint64_t whole = n / unit;
    unsigned decimals = whole < 10 ? 3 : whole < 100 ? 2 : whole < 1000 ? 1 : 0;
    unsigned scale = decimals == 3 ? 1000 : decimals == 2 ? 100 : decimals == 1 ? 10 : 1;
    if (decimals) {
        unsigned fraction = (unsigned)((n % unit) / (unit / scale));
        snprintf(buf, cap, "%" PRIu64 ".%0*u%c", whole, (int)decimals, fraction, "KMBT"[suffix]);
    } else snprintf(buf, cap, "%" PRIu64 "%c", whole, "KMBT"[suffix]);
}

void app_fmt_ago(int64_t now_ms, int64_t at_ms, char *buf, size_t cap) {
    int64_t delta = now_ms - at_ms;
    if (delta < 60 * 1000) {
        snprintf(buf, cap, "刚刚");
    } else if (delta < 60 * 60 * 1000) {
        snprintf(buf, cap, "%d分钟前", (int)(delta / (60 * 1000)));
    } else if (delta < 24 * 60 * 60 * 1000) {
        snprintf(buf, cap, "%d小时前", (int)(delta / (60 * 60 * 1000)));
    } else {
        snprintf(buf, cap, "%d天前", (int)(delta / (24 * 60 * 60 * 1000)));
    }
}

void app_fmt_tokens(double value, char *buf, size_t cap) {
    if (value < 0) value = 0;
    if (value >= 10000.0) {
        double wan = value / 10000.0;
        if (wan >= 100.0) {
            snprintf(buf, cap, "%d万", (int)(wan + 0.5));
        } else {
            snprintf(buf, cap, "%.1f万", wan);
        }
    } else {
        snprintf(buf, cap, "%d", (int)(value + 0.5));
    }
}

void app_fmt_clock(int64_t remaining_ms, char *buf, size_t cap) {
    if (remaining_ms < 0) remaining_ms = 0;
    // 就近取整到秒,边界 24:59.7 显示 25:00 而不是 24:59。
    int total_sec = (int)((remaining_ms + 500) / 1000);
    snprintf(buf, cap, "%d:%02d", total_sec / 60, total_sec % 60);
}

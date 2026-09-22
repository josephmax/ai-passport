// main/app/app_fmt.c —— 展示格式化实现。纯 C11。
#include "app_fmt.h"

#include <stdio.h>
#include <string.h>

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

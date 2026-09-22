// main/app/app_time.c —— 时间辅助实现。纯 C11。
#include "app_time.h"

#include <stddef.h>

bool app_time_in_rest(int min_of_day, app_rest_window_t window) {
    if (window.start_min == window.end_min) return false;
    if (window.start_min < window.end_min) {
        return min_of_day >= window.start_min && min_of_day < window.end_min;
    }
    // 跨午夜:两段 [start,1440) + [0,end)。
    return min_of_day >= window.start_min || min_of_day < window.end_min;
}

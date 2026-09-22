// main/app/app_weather.h —— 天气与时段(纯逻辑)。规则见规格 §5.2。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// WMO 天气码归一化:0–2 晴;3/45/48 阴;51–67/80–82 雨;71–77/85–86 雪;未知归阴。
typedef enum {
    APP_WEATHER_SUNNY = 0,
    APP_WEATHER_CLOUDY,
    APP_WEATHER_RAIN,
    APP_WEATHER_SNOW,
} app_weather_kind_t;

app_weather_kind_t app_weather_from_wmo(int wmo_code);

// 时段:白天=sunrise+30 ~ sunset−60;傍晚=sunset−60 ~ sunset+45;其余夜间。
typedef enum {
    APP_TOD_DAY = 0,
    APP_TOD_DUSK,
    APP_TOD_NIGHT,
} app_tod_t;

// 切换滞回(规格:两端留 10 分钟余量,避免边界闪烁)。
#define APP_TOD_HYSTERESIS_MIN 10

// 带滞回的时段判定:仅当候选状态在 (min - 10) 处也成立才切换。prev 为当前
// 粘滞状态;首次调用传 app_tod_raw 的结果或 APP_TOD_DAY。
app_tod_t app_tod_compute(int min_of_day, int sunrise_min, int sunset_min,
                          app_tod_t prev);

// 无滞回的裸判定(给 prev 一个合理初值用)。
app_tod_t app_tod_raw(int min_of_day, int sunrise_min, int sunset_min);

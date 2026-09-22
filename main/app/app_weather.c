// main/app/app_weather.c —— 天气与时段实现。纯 C11。
#include "app_weather.h"

#include <stddef.h>

app_weather_kind_t app_weather_from_wmo(int code) {
    if (code >= 0 && code <= 2) return APP_WEATHER_SUNNY;
    if (code == 3 || code == 45 || code == 48) return APP_WEATHER_CLOUDY;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        return APP_WEATHER_RAIN;
    }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        return APP_WEATHER_SNOW;
    }
    return APP_WEATHER_CLOUDY;   // 未知码保守归阴
}

app_tod_t app_tod_raw(int min_of_day, int sunrise_min, int sunset_min) {
    int day_start = sunrise_min + 30;
    int dusk_start = sunset_min - 60;
    int dusk_end = sunset_min + 45;
    if (day_start <= min_of_day && min_of_day < dusk_start) return APP_TOD_DAY;
    if (dusk_start <= min_of_day && min_of_day < dusk_end) return APP_TOD_DUSK;
    return APP_TOD_NIGHT;
}

app_tod_t app_tod_compute(int min_of_day, int sunrise_min, int sunset_min,
                          app_tod_t prev) {
    app_tod_t cur = app_tod_raw(min_of_day, sunrise_min, sunset_min);
    if (cur == prev) return cur;
    // 候选状态须连续成立 10 分钟才切换;边界前后 10 分钟内维持 prev。
    int back = min_of_day - APP_TOD_HYSTERESIS_MIN;
    if (back < 0) back += 1440;
    if (app_tod_raw(back, sunrise_min, sunset_min) == cur) return cur;
    return prev;
}

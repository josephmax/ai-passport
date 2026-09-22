// tests/test_app_weather.c —— WMO 归一化 + 时段判定/滞回主机单测(规格 §5.2)。
#include "app/app_weather.h"

#include <assert.h>
#include <stdio.h>

static void test_wmo_mapping(void) {
    assert(app_weather_from_wmo(0) == APP_WEATHER_SUNNY);
    assert(app_weather_from_wmo(1) == APP_WEATHER_SUNNY);
    assert(app_weather_from_wmo(2) == APP_WEATHER_SUNNY);
    assert(app_weather_from_wmo(3) == APP_WEATHER_CLOUDY);
    assert(app_weather_from_wmo(45) == APP_WEATHER_CLOUDY);
    assert(app_weather_from_wmo(48) == APP_WEATHER_CLOUDY);
    for (int c = 51; c <= 67; c++) assert(app_weather_from_wmo(c) == APP_WEATHER_RAIN);
    for (int c = 80; c <= 82; c++) assert(app_weather_from_wmo(c) == APP_WEATHER_RAIN);
    for (int c = 71; c <= 77; c++) assert(app_weather_from_wmo(c) == APP_WEATHER_SNOW);
    assert(app_weather_from_wmo(85) == APP_WEATHER_SNOW);
    assert(app_weather_from_wmo(86) == APP_WEATHER_SNOW);
    assert(app_weather_from_wmo(4) == APP_WEATHER_CLOUDY);    // 未知归阴
    assert(app_weather_from_wmo(999) == APP_WEATHER_CLOUDY);
    printf("wmo_mapping ok\n");
}

static void test_tod_raw_boundaries(void) {
    int sr = 6 * 60 + 12;    // 06:12
    int ss = 18 * 60 + 5;    // 18:05
    // 白天 = sr+30(372+30=402) ~ ss-60(1085-60=1025... 18:05=1085,1025=17:05)
    assert(app_tod_raw(401, sr, ss) == APP_TOD_NIGHT);
    assert(app_tod_raw(402, sr, ss) == APP_TOD_DAY);
    assert(app_tod_raw(1024, sr, ss) == APP_TOD_DAY);
    // 傍晚 = 1025 ~ ss+45=1130
    assert(app_tod_raw(1025, sr, ss) == APP_TOD_DUSK);
    assert(app_tod_raw(1129, sr, ss) == APP_TOD_DUSK);
    assert(app_tod_raw(1130, sr, ss) == APP_TOD_NIGHT);
    assert(app_tod_raw(0, sr, ss) == APP_TOD_NIGHT);
    printf("tod_raw_boundaries ok\n");
}

static void test_tod_hysteresis(void) {
    int sr = 6 * 60 + 12, ss = 18 * 60 + 5;
    int day_start = sr + 30;
    // 夜 -> 日:边界后 10 分钟内维持夜,第 10 分钟切换
    assert(app_tod_compute(day_start + 9, sr, ss, APP_TOD_NIGHT) == APP_TOD_NIGHT);
    assert(app_tod_compute(day_start + 10, sr, ss, APP_TOD_NIGHT) == APP_TOD_DAY);
    // 已是白天则不粘滞
    assert(app_tod_compute(day_start + 1, sr, ss, APP_TOD_DAY) == APP_TOD_DAY);
    // 日 -> 傍晚
    int dusk_start = ss - 60;
    assert(app_tod_compute(dusk_start + 9, sr, ss, APP_TOD_DAY) == APP_TOD_DAY);
    assert(app_tod_compute(dusk_start + 10, sr, ss, APP_TOD_DAY) == APP_TOD_DUSK);
    // 傍晚 -> 夜
    int dusk_end = ss + 45;
    assert(app_tod_compute(dusk_end + 9, sr, ss, APP_TOD_DUSK) == APP_TOD_DUSK);
    assert(app_tod_compute(dusk_end + 10, sr, ss, APP_TOD_DUSK) == APP_TOD_NIGHT);
    // 跨午夜回退不越界(凌晨 0:00-10 分处 raw(-10) 落到前一天)
    assert(app_tod_compute(5, sr, ss, APP_TOD_NIGHT) == APP_TOD_NIGHT);
    printf("tod_hysteresis ok\n");
}

int main(void) {
    test_wmo_mapping();
    test_tod_raw_boundaries();
    test_tod_hysteresis();
    printf("test_app_weather: all passed\n");
    return 0;
}

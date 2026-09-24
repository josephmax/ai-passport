// main/app/app_focus.h —— 番茄计时状态机（纯逻辑，不依赖 ESP-IDF/LVGL，可主机单测）。
//
// 主屏调整 n=0..5 后确认:从确认时刻重新计时 n×25 分钟;0 取消。
// 单元边界静默 +1 经验，全部结束才播胜利。草稿独立于此持久态。
// 状态只存绝对量（结束时间戳/单元数/已授经验数），掉电重启后用 app_focus_poll
// 循环回放即可补齐断电期间越过的边界 —— 无需单独的"恢复"入口。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef PENDANT_FAST_FOCUS
// 调试构建(main/CMakeLists 由 PENDANT_FAST_FOCUS 环境变量注入):单元缩到
// 10 秒,用于真机快速验证 边界授经验/胜利动画/音效 链路。正式构建不定义。
#define APP_FOCUS_UNIT_MS (10LL * 1000LL)
#else
#define APP_FOCUS_UNIT_MS (25LL * 60LL * 1000LL)   // 一个番茄计时单元 25 分钟
#endif
#define APP_FOCUS_MAX_UNITS 5                       // 最多并存 5 个单元(125 分钟)

typedef enum {
    APP_FOCUS_EV_NONE = 0,
    APP_FOCUS_EV_STARTED,      // IDLE -> RUNNING(n=1)
    APP_FOCUS_EV_UNIT_ADDED,   // RUNNING n -> n+1
    APP_FOCUS_EV_STACK_FULL,   // n=5 再按:状态不变,UI 播短促提示音
    APP_FOCUS_EV_CANCELLED,    // OK 长按:清零,未完成单元不记经验
    APP_FOCUS_EV_UNIT_DONE,    // 一个 25 分钟边界静默越过:调用方 +1 经验
    APP_FOCUS_EV_VICTORY,      // 全部单元结束:播放胜利动画+音效
} app_focus_ev_t;

typedef struct {
    bool running;
    uint8_t units;       // 1..APP_FOCUS_MAX_UNITS,当前并存的单元数
    uint8_t xp_granted;  // 已折算成经验的单元数(断电续算/防重复授经验的锚点)
    int64_t end_at_ms;   // 全部单元排空的绝对时刻(本地历元毫秒)
} app_focus_state_t;

void app_focus_reset(app_focus_state_t *st);

// Caller settles already elapsed unit boundaries before replacing the timer.
// Invalid units leave state unchanged. External lifetime XP is never reset.
app_focus_ev_t app_focus_configure(app_focus_state_t *st, uint8_t units, int64_t now_ms);

// OK 短按的两个入口:未计时 -> start;计时中 -> add_unit。
app_focus_ev_t app_focus_start(app_focus_state_t *st, int64_t now_ms);
app_focus_ev_t app_focus_add_unit(app_focus_state_t *st, int64_t now_ms);
app_focus_ev_t app_focus_cancel(app_focus_state_t *st);

// 周期推进:每次调用最多吐一个事件。断电重启后连续 poll 到 NONE 即补齐欠账;
// 事件顺序保证先 drain 完所有 UNIT_DONE,最后才吐 VICTORY。
app_focus_ev_t app_focus_poll(app_focus_state_t *st, int64_t now_ms);

// 剩余总时长(ms),未计时或已到点返回 0。
int64_t app_focus_remaining_ms(const app_focus_state_t *st, int64_t now_ms);
// 距下一个"单元边界或胜利"的毫秒数;未计时返回 -1。浅睡定时器用它定唤醒点。
int64_t app_focus_next_boundary_ms(const app_focus_state_t *st, int64_t now_ms);

// main/app/app_power.h —— 息屏/浅睡/深睡管理(ADR-0003)。
//
// 到达自动息屏阈值:熄屏广播;番茄计时中 → 浅睡片(定时器到点或任意按键
// GPIO0 低电平唤醒);无计时 → 10 秒宽限后深睡(任意按键唤醒=重启)。
// 亮屏由按键活动触发(app_power_notify_activity)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 启动监控任务(幂等)。依赖 button/display/audio/battery 已初始化。
bool app_power_init(void);

// 任何按键/触摸活动时由 Shell 调用;若当前熄屏则广播亮屏事件。
void app_power_notify_activity(void);

// 当前是否处于熄屏状态(Shell 据此暂停宠物动画等高耗绘制)。
bool app_power_screen_off(void);

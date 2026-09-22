// main/app/app_debug.h —— 统一观测点:真机 debug 的"解剖套件"。
//
// 心跳(10s,电源任务)输出一行含:开机秒数、系统堆、LVGL 池水位/碎片、
// 各任务栈余量;每次按键/切页/模式切换有面包屑;开机报复位原因与
// 上次运行时长(RTC 快存,软复位保留)——崩溃时刻可回推。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 注册需要监控栈余量的任务(名字仅用于日志)。
void app_debug_register(const char *name, void *task_handle);

// 心跳调用:输出一行总览。返回 true 表示本次心跳应写 NVS 运行时长。
bool app_debug_heartbeat(void);

// 开机调用:报告复位原因 + 上次运行时长。
void app_debug_boot_report(void);

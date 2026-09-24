// main/app/app_service.h —— 周期维护服务(亮屏由输入工作任务驱动,浅睡间隙
// 由电源任务驱动,两处共用同一入口,内部互斥)。
//
// 职责:番茄状态机推进(授经验/胜利)、今日/本周跨界回零、时段滞回推进、
// 墙钟的 NVS 影子(断电重启后用它恢复一个"冻结的最后已知时间",
// 真正校准靠快照 generatedAt —— 规格风险 #5 接受离线漂移)。
#pragma once

#include "app_time.h"

#include <stdbool.h>
#include <stdint.h>

// 开机时恢复 NVS 里的最后已知墙钟(若有)。在 app_store_init 之后调用。
void app_service_init(void);

// 幂等推进;now_ms 为本地历元毫秒(esp_timer 校准后的系统时间)。
// 返回后通过 app_runtime_publish 广播相应事件。
void app_service_tick(int64_t now_ms);

// Worker-only: settle earned XP and replace/cancel atomically with periodic polling.
void app_service_configure_focus(uint8_t units, int64_t now_ms);

// 立即把墙钟影子写盘(深睡前调用)。
void app_service_flush_clock(void);

// main/ui/ui_dash.h —— 仪表盘页(规格 §4)。
#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *screen;
} ui_dash_t;

// 构建页面(持 LVGL 锁调用)。三卡主界面 + 下钻详情两套视图。
ui_dash_t *ui_dash_create(void);

// UP/DOWN 已被 Shell 用于切页;下钻页的 UP/DOWN(三项详情间切换)由此处理。
// 返回 true 表示事件已消费(下钻页内切换)。
bool ui_dash_key(bool ok_short, bool ok_long, bool up, bool down);

// 数据/状态刷新(1Hz 与事件驱动均可,幂等)。
void ui_dash_refresh(void);

lv_obj_t *ui_dash_screen(void);

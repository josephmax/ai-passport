// main/ui/ui_dash.h —— 仪表盘页(规格 §4)。
#pragma once

#include "lvgl.h"
#include "ui_theme.h"

typedef struct {
    lv_obj_t *screen;
} ui_dash_t;

// 构建页面(持 LVGL 锁调用)。三卡主界面 + 下钻详情两套视图。
ui_dash_t *ui_dash_create(void);
void ui_dash_destroy(void);

// List: UP/DOWN selects a quota; detail: switches quota.
// False only for long OK on the list (return home).
bool ui_dash_key(bool ok_short, bool ok_long, bool up, bool down);

// 数据/状态刷新(1Hz 与事件驱动均可,幂等)。
void ui_dash_refresh(void);

lv_obj_t *ui_dash_screen(void);

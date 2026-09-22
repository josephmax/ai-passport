// main/ui/ui_settings.h —— 设置页(规格 §7)。
#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *screen;
} ui_settings_t;

ui_settings_t *ui_settings_create(void);
lv_obj_t *ui_settings_screen(void);

// 键处理:返回 true 表示已消费。
// 布局:列表内 UP/DOWN 移动选择、OK 进入/循环调整、OK 长按退回;
// (规格 §3 键位表在设置页的"上下=切页"与条目选择互斥,按可操作性取舍)
bool ui_settings_key(bool ok_short, bool ok_long, bool up, bool down);

void ui_settings_refresh(void);

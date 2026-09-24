// main/ui/ui_pet.h —— 宠物页(规格 §5):三动作 + 胜利 + 地图滚动 + 天气/时段。
#pragma once

#include "lvgl.h"
#include "ui_theme.h"
#include "app_home.h"

typedef struct {
    lv_obj_t *screen;
} ui_pet_t;

ui_pet_t *ui_pet_create(void);
// 销毁页面控件;先停止动画定时器。主屏正常常驻,二级页按需创建与销毁。
void ui_pet_destroy(void);
lv_obj_t *ui_pet_screen(void);

void ui_pet_set_navigation(const app_home_t *home);
bool ui_pet_victory_active(void);

// 1Hz 数据刷新(头条、HUD、番茄与光标,幂等)。
void ui_pet_refresh(void);

// 触发一次性胜利动画(约 2s,期间动作锁定为 victory)。
void ui_pet_on_victory(void);

// 熄屏时暂停动画推进(电源管理事件驱动)。
void ui_pet_set_paused(bool paused);

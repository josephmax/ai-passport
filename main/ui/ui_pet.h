// main/ui/ui_pet.h —— 宠物页(规格 §5):三动作 + 胜利 + 地图滚动 + 天气/时段。
#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *screen;
} ui_pet_t;

ui_pet_t *ui_pet_create(void);
lv_obj_t *ui_pet_screen(void);

// 1Hz 数据刷新(HUD/信息条,幂等)。
void ui_pet_refresh(void);

// 触发一次性胜利动画(约 2s,期间动作锁定为 victory)。
void ui_pet_on_victory(void);

// 熄屏时暂停动画推进(电源管理事件驱动)。
void ui_pet_set_paused(bool paused);

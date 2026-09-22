// main/ui/ui_pet.h —— 宠物页(规格 §5):三动作 + 胜利 + 地图滚动 + 天气/时段。
#pragma once

#include "lvgl.h"
#include "ui_theme.h"

typedef struct {
    lv_obj_t *screen;
} ui_pet_t;

ui_pet_t *ui_pet_create(void);
// 销毁页面控件(素材堆缓冲与已加载帧保留,重建不重读);
// 规格 §10:页面切换销毁旧屏再建新屏,三页常驻曾把 48KB LVGL 池
// 挤到水位 95%+,所有图片渲染断供(真机现象:仅文字无图)。
void ui_pet_destroy(void);
lv_obj_t *ui_pet_screen(void);

// 本页的页点指示器(Shell 切页时置激活态)。
ui_page_dots_t *ui_pet_dots(void);

// 1Hz 数据刷新(HUD/信息条,幂等)。
void ui_pet_refresh(void);

// 触发一次性胜利动画(约 2s,期间动作锁定为 victory)。
void ui_pet_on_victory(void);

// 熄屏时暂停动画推进(电源管理事件驱动)。
void ui_pet_set_paused(bool paused);

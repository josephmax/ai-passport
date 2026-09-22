// main/ui/ui_theme.h —— 应用视觉主题"夜航"(衍生应用全新设计,不复用
// 基线 demo 的 ui_pixel 纸墨外壳)。
//
// 深海军蓝底 + 薄荷强调色;卡片圆角 12px;数字用 Montserrat,中文用
// app_font_12/24 像素字体。所有页面经 ui_theme_screen 创建统一底色。
#pragma once

#include "lvgl.h"

// 调色板
#define UI_BG        0x0E1626   // 页面底
#define UI_SURFACE   0x1C2A44   // 卡片面
#define UI_SURFACE_HI 0x24365A  // 选中卡片面
#define UI_LINE      0x2E4066   // 描边
#define UI_INK       0xE8F0FF   // 主文字
#define UI_INK_DIM   0x8FA3C8   // 次文字
#define UI_ACCENT    0x7EE8B2   // 薄荷(选中/正向)
#define UI_WARN      0xFF8A80   // 珊瑚(超额/警示)
#define UI_GOLD      0xFFD154   // 经验/等级

LV_FONT_DECLARE(app_font_12);
LV_FONT_DECLARE(app_font_24);

// 统一页面:底色、关闭滚动、返回屏幕对象。
lv_obj_t *ui_theme_screen(void);

// 圆角卡片;selected 高亮边框。
lv_obj_t *ui_theme_card(lv_obj_t *parent, int x, int y, int w, int h,
                        bool selected);

// 顶部状态条:同步时间(左) + Wi-Fi 图标 + 电量(右上角,读失败自动隐藏)。
// 返回容器;内部句柄由 ui_theme_status_bar_refresh 刷新。
typedef struct {
    lv_obj_t *root;
    lv_obj_t *sync;
    lv_obj_t *wifi;
    lv_obj_t *battery;
} ui_status_bar_t;

void ui_theme_status_bar_create(lv_obj_t *parent, ui_status_bar_t *out);
void ui_theme_status_bar_refresh(ui_status_bar_t *bar);

// 小圆点页面指示器(三个一级页)。
void ui_theme_page_dots(lv_obj_t *parent, int active);

// 亮度档位 1..5 → 背光百分比并立即生效。
void ui_theme_brightness_apply(uint8_t level);

// Shared compact "Night voyage" styles; callers hold the LVGL lock.
#pragma once
#include "lvgl.h"

#define UI_BG         0x0E1626
#define UI_SURFACE    0x1B2940
#define UI_SURFACE_HI 0x24394B
#define UI_LINE       0x334763
#define UI_INK        0xE8F0FF
#define UI_INK_DIM    0xA3B4CF
#define UI_ACCENT     0x7EE8B2
#define UI_WARN       0xFFAAA0
#define UI_GOLD       0xFFD154

LV_FONT_DECLARE(app_font_12);
LV_FONT_DECLARE(app_font_24);

lv_obj_t *ui_theme_screen(void);
lv_obj_t *ui_theme_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color);
lv_obj_t *ui_theme_label(lv_obj_t *parent, int x, int y, int w,
                         const lv_font_t *font, uint32_t color, const char *text);
lv_obj_t *ui_theme_card(lv_obj_t *parent, int x, int y, int w, int h, bool selected);
void ui_theme_select(lv_obj_t *obj, bool selected);

typedef struct { lv_obj_t *root, *sync, *wifi, *battery; } ui_status_bar_t;
void ui_theme_status_bar_create(lv_obj_t *parent, ui_status_bar_t *out);
void ui_theme_status_bar_refresh(ui_status_bar_t *bar);
lv_obj_t *ui_theme_hint_create(lv_obj_t *parent);
void ui_theme_hint_set(lv_obj_t *hint, const char *updown, const char *ok, const char *oklong);
void ui_theme_brightness_apply(uint8_t level);

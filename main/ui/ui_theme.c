#include "ui_theme.h"
#include "app_fmt.h"
#include "app_runtime.h"
#include "bsp_display.h"
#include <stdio.h>
#include <sys/time.h>

lv_obj_t *ui_theme_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t *ui_theme_screen(void) {
    return ui_theme_box(NULL, 0, 0, 240, 320, UI_BG);
}

lv_obj_t *ui_theme_label(lv_obj_t *parent, int x, int y, int w,
                         const lv_font_t *font, uint32_t color, const char *text) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, w);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_text(obj, text);
    return obj;
}

void ui_theme_select(lv_obj_t *obj, bool selected) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(selected ? UI_SURFACE_HI : UI_SURFACE), 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_outline_color(obj, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_outline_width(obj, selected ? 2 : 0, 0);
    lv_obj_set_style_outline_pad(obj, -2, 0);
    // Padding stays constant when selection changes.
    lv_obj_set_style_pad_all(obj, 0, 0);
}

lv_obj_t *ui_theme_card(lv_obj_t *parent, int x, int y, int w, int h, bool selected) {
    lv_obj_t *obj = ui_theme_box(parent, x, y, w, h, UI_SURFACE);
    lv_obj_set_style_radius(obj, 6, 0);
    ui_theme_select(obj, selected);
    return obj;
}

void ui_theme_status_bar_create(lv_obj_t *parent, ui_status_bar_t *out) {
    out->root = ui_theme_box(parent, 10, 4, 220, 16, UI_BG);
    out->sync = ui_theme_label(out->root, 0, 1, 151, &app_font_12, UI_INK_DIM, "");
    out->wifi = ui_theme_label(out->root, 164, 0, 18, &lv_font_montserrat_14, UI_INK_DIM, LV_SYMBOL_WIFI);
    out->battery = ui_theme_label(out->root, 183, 1, 37, &app_font_12, UI_INK_DIM, "");
    lv_obj_set_style_text_align(out->battery, LV_TEXT_ALIGN_RIGHT, 0);
}

void ui_theme_status_bar_refresh(ui_status_bar_t *bar) {
    app_runtime_t *rt = app_runtime();
    char buf[48];
    if (!rt->snap_valid) snprintf(buf, sizeof(buf), "%s", rt->paired ? "等待同步" : "未配网");
    else {
        const app_snapshot_t *snap = app_runtime_snap();
        struct timeval tv;
        gettimeofday(&tv, NULL);
        int64_t now = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
        if (!rt->wifi_connected) {
            int hours = app_snapshot_offline_hours(snap->generated_at_ms, now);
            snprintf(buf, sizeof(buf), "离线 %d 小时", hours);
        } else {
            char age[24];
            app_fmt_ago(now, snap->generated_at_ms, age, sizeof(age));
            snprintf(buf, sizeof(buf), "同步于 %s", age);
        }
    }
    lv_label_set_text(bar->sync, buf);
    lv_obj_set_style_text_color(bar->sync, lv_color_hex(rt->snap_valid && !rt->wifi_connected ? UI_WARN : UI_INK_DIM), 0);
    lv_label_set_text(bar->wifi, rt->wifi_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);
    if (rt->battery_soc < 0) lv_obj_add_flag(bar->battery, LV_OBJ_FLAG_HIDDEN);
    else {
        lv_obj_clear_flag(bar->battery, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(bar->battery, "%d%%", rt->battery_soc);
    }
}

lv_obj_t *ui_theme_hint_create(lv_obj_t *parent) {
    lv_obj_t *obj = ui_theme_label(parent, 0, 303, 240, &app_font_12, UI_INK_DIM, "");
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    return obj;
}

void ui_theme_hint_set(lv_obj_t *hint, const char *updown, const char *ok, const char *oklong) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s%s%s%s%s%s%s%s",
             updown ? "上下 " : "", updown ? updown : "",
             updown && ok ? "  " : "", ok ? "OK " : "", ok ? ok : "",
             (updown || ok) && oklong ? "  " : "", oklong ? "长按 " : "", oklong ? oklong : "");
    lv_label_set_text(hint, buf);
}

void ui_theme_brightness_apply(uint8_t level) {
    static const uint8_t MAP[6] = {0, 8, 25, 45, 70, 100};
    if (level < 1) level = 1;
    if (level > 5) level = 5;
    bsp_display_backlight(MAP[level]);
}

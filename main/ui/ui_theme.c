// main/ui/ui_theme.c —— 主题实现。
#include "ui_theme.h"

#include "app_fmt.h"
#include "app_runtime.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "lvgl.h"

#include <stdio.h>
#include <sys/time.h>

lv_obj_t *ui_theme_screen(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    return scr;
}

lv_obj_t *ui_theme_card(lv_obj_t *parent, int x, int y, int w, int h,
                        bool selected) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(selected ? UI_SURFACE_HI : UI_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(selected ? UI_ACCENT : UI_LINE), 0);
    lv_obj_set_style_border_width(card, selected ? 2 : 1, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

void ui_theme_status_bar_create(lv_obj_t *parent, ui_status_bar_t *out) {
    out->root = lv_obj_create(parent);
    lv_obj_set_pos(out->root, 0, 0);
    lv_obj_set_size(out->root, 240, 24);
    lv_obj_set_style_bg_opa(out->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(out->root, 0, 0);
    lv_obj_set_style_pad_all(out->root, 0, 0);
    lv_obj_clear_flag(out->root, LV_OBJ_FLAG_SCROLLABLE);

    out->sync = lv_label_create(out->root);
    lv_obj_set_style_text_font(out->sync, &app_font_12, 0);
    lv_obj_set_style_text_color(out->sync, lv_color_hex(UI_INK_DIM), 0);
    lv_label_set_text(out->sync, "");
    lv_obj_align(out->sync, LV_ALIGN_LEFT_MID, 6, 0);

    out->wifi = lv_label_create(out->root);
    lv_obj_set_style_text_font(out->wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(out->wifi, lv_color_hex(UI_INK_DIM), 0);
    lv_label_set_text(out->wifi, LV_SYMBOL_CLOSE);
    lv_obj_align(out->wifi, LV_ALIGN_RIGHT_MID, -58, 0);

    out->battery = lv_label_create(out->root);
    lv_obj_set_style_text_font(out->battery, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(out->battery, lv_color_hex(UI_INK_DIM), 0);
    lv_label_set_text(out->battery, "");
    lv_obj_align(out->battery, LV_ALIGN_RIGHT_MID, -6, 0);
}

void ui_theme_status_bar_refresh(ui_status_bar_t *bar) {
    app_runtime_t *rt = app_runtime();
    char buf[40];

    if (!rt->snap_valid) {
        snprintf(buf, sizeof(buf), "%s", rt->paired ? "等待同步" : "未配网");
    } else {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        int64_t now_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
        char ago[24];
        app_fmt_ago(now_ms, rt->snap.generated_at_ms, ago, sizeof(ago));
        int offline_h = app_snapshot_offline_hours(rt->snap.generated_at_ms, now_ms);
        if (offline_h >= 1) {
            snprintf(buf, sizeof(buf), "离线 %d 小时", offline_h);
        } else {
            snprintf(buf, sizeof(buf), "%s", ago);
        }
    }
    lv_label_set_text(bar->sync, buf);
    lv_label_set_text(bar->wifi, rt->wifi_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_obj_add_flag(bar->battery, LV_OBJ_FLAG_HIDDEN);   // 读失败优雅隐藏
    } else {
        lv_obj_clear_flag(bar->battery, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(bar->battery, "%d%%", soc);
    }
}

void ui_theme_page_dots(lv_obj_t *parent, int active) {    static lv_obj_t *dots[3];
    static lv_obj_t *holder;
    if (!holder || lv_obj_get_parent(holder) != parent) {
        holder = lv_obj_create(parent);
        lv_obj_set_pos(holder, 96, 302);
        lv_obj_set_size(holder, 48, 12);
        lv_obj_set_style_bg_opa(holder, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(holder, 0, 0);
        lv_obj_set_style_pad_all(holder, 0, 0);
        lv_obj_set_flex_flow(holder, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_flex_main_place(holder, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
        lv_obj_clear_flag(holder, LV_OBJ_FLAG_SCROLLABLE);
        for (int i = 0; i < 3; i++) {
            dots[i] = lv_obj_create(holder);
            lv_obj_set_size(dots[i], 8, 8);
            lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(dots[i], 0, 0);
            lv_obj_set_style_bg_opa(dots[i], LV_OPA_COVER, 0);
        }
    }
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(dots[i],
            lv_color_hex(i == active ? UI_ACCENT : UI_LINE), 0);
    }
}

void ui_theme_brightness_apply(uint8_t level) {
    // 5 档 → 背光百分比;1 档也要可见。level 收敛到 1..5。
    static const uint8_t MAP[6] = { 0, 8, 25, 45, 70, 100 };
    if (level < 1) level = 1;
    if (level > 5) level = 5;
    bsp_display_backlight(MAP[level]);
}

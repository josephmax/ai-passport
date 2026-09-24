#include "ui_settings.h"
#include "app_audio_fx.h"
#include "app_prov.h"
#include "app_runtime.h"
#include "app_sync.h"
#include "ui_theme.h"
#include <stdio.h>
#include <string.h>

typedef enum { BRIGHTNESS, WIFI, SCREEN_OFF, REST, CONNECT, VOLUME, SYNC, ITEM_COUNT } item_t;
typedef enum { LIST, ADJUST, REST_EDIT, CONNECT_FLOW, PROV_FLOW } settings_mode_t;
static const char *NAMES[] = {"亮度", "Wi-Fi", "自动息屏", "作息时间", "连接手机", "音量", "立即同步"};
static const uint16_t OFF_STEPS[] = {15, 30, 60, 120};
static ui_settings_t *s_settings;
static ui_status_bar_t s_status;
static lv_obj_t *s_content, *s_hint, *s_title, *s_rows[ITEM_COUNT], *s_values[ITEM_COUNT];
static lv_obj_t *s_value, *s_steps[5], *s_rest_rows[2], *s_rest_labels[2], *s_flow_text;
static settings_mode_t s_mode;
static item_t s_selected;
static app_settings_t s_entry, s_draft;
static int s_rest_pick;
static ui_settings_work_t s_work;
static bool s_sync_requested;

static int off_index(uint16_t value) {
    for (int i = 0; i < 4; i++) if (OFF_STEPS[i] == value) return i;
    return 1;
}

static void schedule_save(void) {
    app_runtime_t *rt = app_runtime();
    rt->rest = (app_rest_window_t){.start_min = rt->settings.rest_start_min, .end_min = rt->settings.rest_end_min};
    s_work.settings = rt->settings;
    s_work.save = true;
}

static void cancel_edit(void) {
    if (s_mode == ADJUST) {
        app_runtime()->settings = s_entry;
        ui_theme_brightness_apply(s_entry.brightness);
    }
    if (s_mode == PROV_FLOW) s_work.cancel_provisioning = true;
}

ui_settings_work_t ui_settings_take_work(void) {
    ui_settings_work_t work = s_work;
    memset(&s_work, 0, sizeof(s_work));
    return work;
}

static void value_text(item_t item, char *buf, size_t cap) {
    app_runtime_t *rt = app_runtime();
    switch (item) {
    case BRIGHTNESS: snprintf(buf, cap, "%u / 5", rt->settings.brightness); break;
    case VOLUME: snprintf(buf, cap, "%u / 5", rt->settings.volume); break;
    case SCREEN_OFF:
        if (rt->settings.auto_off_s < 60) snprintf(buf, cap, "%u 秒", rt->settings.auto_off_s);
        else snprintf(buf, cap, "%u 分钟", rt->settings.auto_off_s / 60);
        break;
    case REST: snprintf(buf, cap, "%02u:%02u-%02u:%02u", rt->settings.rest_start_min / 60,
        rt->settings.rest_start_min % 60, rt->settings.rest_end_min / 60, rt->settings.rest_end_min % 60); break;
    case WIFI: snprintf(buf, cap, "%s", rt->wifi_connected ? "已连接" : "配网"); break;
    case CONNECT: snprintf(buf, cap, "%s", rt->paired ? "已配对" : "未配对"); break;
    case SYNC:
        snprintf(buf, cap, "%s", !rt->paired ? "未配对" : !rt->wifi_connected ? "未连接" :
                 rt->sync_in_progress ? "同步中" : s_sync_requested ?
                 (rt->sync_result_valid ? (rt->sync_last_ok ? "已同步" : "同步失败") : "请求中") : "执行");
        break;
    default: buf[0] = '\0'; break;
    }
}

void ui_settings_refresh(void) {
    if (!s_settings) return;
    app_runtime_t *rt = app_runtime();
    ui_theme_status_bar_refresh(&s_status);
    char buf[48];
    if (s_mode == LIST) {
        for (int i = 0; i < ITEM_COUNT; i++) {
            ui_theme_select(s_rows[i], i == (int)s_selected);
            if (i != (int)s_selected) lv_obj_set_style_bg_color(s_rows[i], lv_color_hex(UI_BG), 0);
            value_text((item_t)i, buf, sizeof(buf));
            lv_label_set_text(s_values[i], buf);
            lv_obj_set_style_text_color(s_values[i], lv_color_hex(i == (int)s_selected ? UI_ACCENT : UI_INK_DIM), 0);
        }
        if (s_selected == SYNC && s_sync_requested && rt->sync_result_valid)
            ui_theme_hint_set(s_hint, NULL, rt->sync_last_ok ? "同步成功" : "同步失败", "返回");
    } else if (s_mode == ADJUST) {
        value_text(s_selected, buf, sizeof(buf));
        lv_label_set_text(s_value, buf);
        int n = s_selected == BRIGHTNESS ? rt->settings.brightness : s_selected == VOLUME ? rt->settings.volume : off_index(rt->settings.auto_off_s) + 1;
        for (int i = 0; i < 5; i++) lv_obj_set_style_bg_color(s_steps[i], lv_color_hex(i < n ? UI_ACCENT : UI_LINE), 0);
    } else if (s_mode == REST_EDIT) {
        for (int i = 0; i < 2; i++) {
            uint16_t m = i ? s_draft.rest_end_min : s_draft.rest_start_min;
            lv_label_set_text_fmt(s_rest_labels[i], "%02u:%02u", m / 60, m % 60);
            ui_theme_select(s_rest_rows[i], i == s_rest_pick);
        }
        ui_theme_hint_set(s_hint, "调整", s_rest_pick ? "保存" : "下一项", "取消");
    } else if (s_mode == PROV_FLOW) {
        const char *state = "正在启动热点";
        switch (app_prov_state()) {
        case APP_PROV_AP_UP: state = "手机连接热点后完成配网"; break;
        case APP_PROV_CONNECTING: state = "正在连接 Wi-Fi"; break;
        case APP_PROV_PAIRING: state = "正在配对"; break;
        case APP_PROV_DONE: state = "配网完成"; break;
        case APP_PROV_FAILED: state = "失败 · OK 重试"; break;
        default: break;
        }
        lv_label_set_text(s_flow_text, state);
        const char *ap = app_prov_ap_name();
        lv_label_set_text(s_value, ap ? ap : "--");
    }
}

static void show(settings_mode_t mode) {
    if (s_content) lv_obj_delete(s_content);
    s_content = ui_theme_box(s_settings->screen, 0, 24, 240, 274, UI_BG);
    s_mode = mode;
    s_title = ui_theme_label(s_content, 12, 6, 216, &app_font_24, UI_INK, mode == LIST ? "设置" : NAMES[s_selected]);
    if (mode == LIST) {
        for (int i = 0; i < ITEM_COUNT; i++) {
            s_rows[i] = ui_theme_card(s_content, 8, 36 + i * 33, 224, 32, false);
            ui_theme_label(s_rows[i], 10, 10, 90, &app_font_12, UI_INK, NAMES[i]);
            s_values[i] = ui_theme_label(s_rows[i], 102, 10, 112, &app_font_12, UI_INK_DIM, "");
            lv_obj_set_style_text_align(s_values[i], LV_TEXT_ALIGN_RIGHT, 0);
        }
        ui_theme_hint_set(s_hint, "选择", "进入", "返回");
    } else if (mode == ADJUST) {
        s_value = ui_theme_label(s_content, 16, 85, 208, &app_font_24, UI_ACCENT, "");
        for (int i = 0; i < 5; i++) {
            s_steps[i] = ui_theme_box(s_content, 16 + i * 43, 157, 36, 9, UI_LINE);
            lv_obj_set_style_radius(s_steps[i], 2, 0);
            if (s_selected == SCREEN_OFF && i == 4) lv_obj_add_flag(s_steps[i], LV_OBJ_FLAG_HIDDEN);
        }
        ui_theme_label(s_content, 16, 195, 208, &app_font_12, UI_INK_DIM, "即时预览 · 长按恢复原值");
        ui_theme_hint_set(s_hint, "调整", "保存", "取消");
    } else if (mode == REST_EDIT) {
        for (int i = 0; i < 2; i++) {
            s_rest_rows[i] = ui_theme_card(s_content, 12, 58 + i * 82, 216, 62, false);
            ui_theme_label(s_rest_rows[i], 10, 8, 192, &app_font_12, UI_INK_DIM, i ? "结束休息" : "开始休息");
            s_rest_labels[i] = ui_theme_label(s_rest_rows[i], 10, 29, 192, &lv_font_montserrat_28, UI_INK, "");
        }
        ui_theme_label(s_content, 12, 226, 216, &app_font_12, UI_INK_DIM, "每次调整 30 分钟");
    } else if (mode == CONNECT_FLOW) {
        app_runtime_t *rt = app_runtime();
#if LV_USE_QRCODE
        if (rt->service_url[0]) {
            lv_obj_t *qr = lv_qrcode_create(s_content);
            lv_qrcode_set_size(qr, 96);
            lv_qrcode_set_dark_color(qr, lv_color_hex(UI_BG));
            lv_qrcode_set_light_color(qr, lv_color_hex(0xFFFFFF));
            lv_obj_set_pos(qr, 72, 54);
            lv_qrcode_update(qr, rt->service_url, strlen(rt->service_url));
        }
#endif
        lv_obj_t *url = ui_theme_label(s_content, 12, 168, 216, &app_font_12, UI_INK,
            rt->service_url[0] ? rt->service_url : "未配置服务地址");
        lv_label_set_long_mode(url, LV_LABEL_LONG_WRAP);
        lv_obj_set_height(url, 42);
        ui_theme_label(s_content, 12, 222, 216, &app_font_12, UI_INK_DIM, rt->paired ? "令牌正常" : "未配对");
        ui_theme_hint_set(s_hint, NULL, NULL, "返回");
    } else {
        s_value = ui_theme_label(s_content, 12, 94, 216, &app_font_12, UI_ACCENT, "--");
        s_flow_text = ui_theme_label(s_content, 12, 146, 216, &app_font_12, UI_INK_DIM, "");
        ui_theme_hint_set(s_hint, NULL, "重试", "返回");
    }
    ui_settings_refresh();
}

bool ui_settings_key(bool ok_short, bool ok_long, bool up, bool down) {
    if (!s_settings) return false;
    app_runtime_t *rt = app_runtime();
    int dir = up ? 1 : down ? -1 : 0;
    if (s_mode == LIST) {
        if (ok_long) return false;
        if (dir) { s_selected = (item_t)((s_selected + (up ? ITEM_COUNT - 1 : 1)) % ITEM_COUNT); s_sync_requested = false; ui_theme_hint_set(s_hint, "选择", "进入", "返回"); ui_settings_refresh(); }
        else if (ok_short) {
            s_entry = rt->settings;
            if (s_selected == BRIGHTNESS || s_selected == VOLUME || s_selected == SCREEN_OFF) show(ADJUST);
            else if (s_selected == REST) { s_draft = s_entry; s_rest_pick = 0; show(REST_EDIT); }
            else if (s_selected == WIFI) { app_prov_request_start(); show(PROV_FLOW); }
            else if (s_selected == CONNECT) show(CONNECT_FLOW);
            else if (s_selected == SYNC) {
                if (!rt->paired) ui_theme_hint_set(s_hint, NULL, "先连接手机", "返回");
                else if (!rt->wifi_connected) ui_theme_hint_set(s_hint, NULL, "先配网", "返回");
                else {
                    rt->sync_result_valid = false;
                    s_sync_requested = true;
                    app_sync_request_now();
                    ui_theme_hint_set(s_hint, NULL, "同步中", "返回");
                }
                ui_settings_refresh();
            }
        }
    } else if (ok_long) { cancel_edit(); show(LIST); }
    else if (s_mode == ADJUST) {
        if (ok_short) { schedule_save(); show(LIST); }
        else if (dir) {
            if (s_selected == SCREEN_OFF) {
                int i = off_index(rt->settings.auto_off_s) + dir;
                if (i >= 0 && i < 4) rt->settings.auto_off_s = OFF_STEPS[i];
            } else {
                uint8_t *p = s_selected == BRIGHTNESS ? &rt->settings.brightness : &rt->settings.volume;
                int v = *p + dir;
                if (v >= (s_selected == BRIGHTNESS ? 1 : 0) && v <= 5) *p = (uint8_t)v;
                if (s_selected == BRIGHTNESS) ui_theme_brightness_apply(*p);
                else app_audio_fx_play(APP_FX_BEEP);
            }
            ui_settings_refresh();
        }
    } else if (s_mode == REST_EDIT) {
        if (dir) {
            uint16_t *m = s_rest_pick ? &s_draft.rest_end_min : &s_draft.rest_start_min;
            *m = (uint16_t)((*m + dir * 30 + 1440) % 1440);
        } else if (ok_short) {
            if (!s_rest_pick) s_rest_pick = 1;
            else { rt->settings = s_draft; schedule_save(); show(LIST); }
        }
        ui_settings_refresh();
    } else if (s_mode == PROV_FLOW && ok_short && app_prov_state() == APP_PROV_FAILED) app_prov_request_start();
    return true;
}

void ui_settings_destroy(void) {
    if (!s_settings) return;
    cancel_edit();
    lv_obj_delete(s_settings->screen);
    lv_free(s_settings);
    s_settings = NULL;
    s_content = NULL;
}

ui_settings_t *ui_settings_create(void) {
    if (s_settings) return s_settings;
    s_settings = lv_malloc(sizeof(*s_settings));
    s_settings->screen = ui_theme_screen();
    ui_theme_status_bar_create(s_settings->screen, &s_status);
    s_hint = ui_theme_hint_create(s_settings->screen);
    s_selected = BRIGHTNESS;
    s_sync_requested = false;
    show(LIST);
    return s_settings;
}

lv_obj_t *ui_settings_screen(void) { return s_settings ? s_settings->screen : NULL; }

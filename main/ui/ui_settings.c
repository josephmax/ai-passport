// main/ui/ui_settings.c —— 设置页实现。
// 条目:亮度 / 音量 / 自动息屏 / 作息时间 / 连接手机 / 立即同步。
// (Wi-Fi 配网入口在"连接手机"子页;独立配网流程随本地服务线接入)
#include "ui_settings.h"

#include "app_runtime.h"
#include "app_store.h"
#include "bsp_display.h"
#include "ui_theme.h"

#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_set";

typedef enum {
    ITEM_BRIGHTNESS = 0,
    ITEM_VOLUME,
    ITEM_SCREEN_OFF,
    ITEM_REST,
    ITEM_CONNECT,
    ITEM_SYNC,
    ITEM_COUNT,
} item_t;

static ui_settings_t *s_settings;
static ui_status_bar_t s_status;
static lv_obj_t *s_rows[ITEM_COUNT];
static lv_obj_t *s_values[ITEM_COUNT];
static int s_selected;
typedef enum { SUB_NONE = 0, SUB_REST, SUB_CONNECT } sub_mode_t;
static sub_mode_t s_sub_mode;
static int s_rest_pick;               // 0=起 1=止
static lv_obj_t *s_sub_view;
static lv_obj_t *s_rest_labels[2];
static lv_obj_t *s_qr;
static lv_obj_t *s_url_label;
static lv_obj_t *s_token_label;

static const uint16_t AUTO_OFF_STEPS[] = { 15, 30, 60, 120 };

static void save_and_apply(void) {
    app_runtime_t *rt = app_runtime();
    rt->rest.start_min = rt->settings.rest_start_min;
    rt->rest.end_min = rt->settings.rest_end_min;
    app_store_save_settings(&rt->settings);
}

static int auto_off_step_index(uint16_t s) {
    for (int i = 0; i < 4; i++) {
        if (AUTO_OFF_STEPS[i] == s) return i;
    }
    return 1;
}

static void refresh_values(void) {
    app_runtime_t *rt = app_runtime();
    char buf[40];
    snprintf(buf, sizeof(buf), "%d/5", rt->settings.brightness);
    lv_label_set_text(s_values[ITEM_BRIGHTNESS], buf);
    snprintf(buf, sizeof(buf), "%d/5", rt->settings.volume);
    lv_label_set_text(s_values[ITEM_VOLUME], buf);
    snprintf(buf, sizeof(buf), "%d秒",
             rt->settings.auto_off_s >= 60 ? rt->settings.auto_off_s / 60
                                           : rt->settings.auto_off_s);
    if (rt->settings.auto_off_s >= 60) {
        snprintf(buf, sizeof(buf), "%d分", rt->settings.auto_off_s / 60);
    }
    lv_label_set_text(s_values[ITEM_SCREEN_OFF], buf);
    snprintf(buf, sizeof(buf), "%02d:%02d-%02d:%02d",
             rt->settings.rest_start_min / 60, rt->settings.rest_start_min % 60,
             rt->settings.rest_end_min / 60, rt->settings.rest_end_min % 60);
    lv_label_set_text(s_values[ITEM_REST], buf);
    lv_label_set_text(s_values[ITEM_CONNECT], rt->paired ? "已配对" : "未配网");
    lv_label_set_text(s_values[ITEM_SYNC], rt->wifi_connected ? "按 OK 同步" : "未连接");
}

void ui_settings_refresh(void) {
    if (!s_settings) return;
    ui_theme_status_bar_refresh(&s_status);
    for (int i = 0; i < ITEM_COUNT; i++) {
        lv_obj_set_style_bg_color(s_rows[i],
            lv_color_hex(i == s_selected && s_sub_mode == SUB_NONE ? UI_SURFACE_HI : UI_SURFACE), 0);
        lv_obj_set_style_border_color(s_rows[i],
            lv_color_hex(i == s_selected && s_sub_mode == SUB_NONE ? UI_ACCENT : UI_LINE), 0);
    }
    refresh_values();
}

static void enter_rest_view(void) {
    s_sub_mode = SUB_REST;
    s_rest_pick = 0;
    lv_obj_clear_flag(s_sub_view, LV_OBJ_FLAG_HIDDEN);
    app_runtime_t *rt = app_runtime();
    for (int i = 0; i < 2; i++) {
        uint16_t m = i == 0 ? rt->settings.rest_start_min : rt->settings.rest_end_min;
        lv_label_set_text_fmt(s_rest_labels[i], "%s %02d:%02d",
                              i == 0 ? "起始" : "结束", m / 60, m % 60);
        lv_obj_set_style_text_color(s_rest_labels[i],
            lv_color_hex(i == 0 ? UI_ACCENT : UI_INK), 0);
    }
}

static void enter_connect_view(void) {
    s_sub_mode = SUB_CONNECT;
    lv_obj_clear_flag(s_sub_view, LV_OBJ_FLAG_HIDDEN);
    app_runtime_t *rt = app_runtime();
    if (rt->service_url[0]) {
        lv_label_set_text(s_url_label, rt->service_url);
#if LV_USE_QRCODE
        lv_qrcode_update(s_qr, rt->service_url, strlen(rt->service_url));
#endif
    } else {
        lv_label_set_text(s_url_label, "未配置服务地址");
#if LV_USE_QRCODE
        lv_qrcode_update(s_qr, "AI-Passport", 12);
#endif
    }
    lv_label_set_text(s_token_label, rt->paired ? "令牌正常" : "未配对(等待配网)");
}

static void exit_sub(void) {
    s_sub_mode = SUB_NONE;
    lv_obj_add_flag(s_sub_view, LV_OBJ_FLAG_HIDDEN);
    save_and_apply();
    ui_settings_refresh();
}

bool ui_settings_key(bool ok_short, bool ok_long, bool up, bool down) {
    if (!s_settings) return false;
    app_runtime_t *rt = app_runtime();

    if (s_sub_mode == SUB_REST) {
        // 作息子页:上下选起/止,OK +30 分钟,长按退出。
        if (ok_long) {
            exit_sub();
            return true;
        }
        if (up || down) {
            s_rest_pick ^= 1;
        } else if (ok_short) {
            uint16_t *m = s_rest_pick == 0 ? &rt->settings.rest_start_min
                                           : &rt->settings.rest_end_min;
            *m = (uint16_t)((*m + 30) % 1440);
        } else {
            return true;
        }
        for (int i = 0; i < 2; i++) {
            uint16_t m = i == 0 ? rt->settings.rest_start_min
                                : rt->settings.rest_end_min;
            lv_label_set_text_fmt(s_rest_labels[i], "%s %02d:%02d",
                                  i == 0 ? "起始" : "结束", m / 60, m % 60);
            lv_obj_set_style_text_color(s_rest_labels[i],
                lv_color_hex(i == s_rest_pick ? UI_ACCENT : UI_INK), 0);
        }
        return true;
    }
    if (s_sub_mode == SUB_CONNECT) {
        if (ok_long) exit_sub();
        return true;   // 只读页:长按退出
    }

    if (up || down) {
        s_selected = (s_selected + (up ? ITEM_COUNT - 1 : 1)) % ITEM_COUNT;
        ui_settings_refresh();
        return true;
    }
    if (ok_long) return false;   // 未消费:无上级可退
    if (!ok_short) return false;

    switch ((item_t)s_selected) {
    case ITEM_BRIGHTNESS:
        rt->settings.brightness = rt->settings.brightness % 5 + 1;
        ui_theme_brightness_apply(rt->settings.brightness);
        save_and_apply();
        break;
    case ITEM_VOLUME:
        rt->settings.volume = (rt->settings.volume + 1) % 6;
        save_and_apply();
        break;
    case ITEM_SCREEN_OFF: {
        int idx = auto_off_step_index(rt->settings.auto_off_s);
        rt->settings.auto_off_s = AUTO_OFF_STEPS[(idx + 1) % 4];
        save_and_apply();
        break;
    }
    case ITEM_REST:
        enter_rest_view();
        break;
    case ITEM_CONNECT:
        enter_connect_view();
        break;
    case ITEM_SYNC:
        // 同步任务随本地服务线接入;未配网时仅提示。
        ESP_LOGI(TAG, "手动同步请求(paired=%d)", rt->paired);
        break;
    default:
        break;
    }
    ui_settings_refresh();
    return true;
}

ui_settings_t *ui_settings_create(void) {
    if (s_settings) return s_settings;
    lv_obj_t *scr = ui_theme_screen();
    s_settings = lv_malloc(sizeof(ui_settings_t));
    s_settings->screen = scr;
    ui_theme_status_bar_create(scr, &s_status);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &app_font_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_INK), 0);
    lv_label_set_text(title, "设置");
    lv_obj_set_pos(title, 12, 30);

    static const char *NAMES[ITEM_COUNT] = {
        "亮度", "音量", "自动息屏", "作息时间", "连接手机", "立即同步",
    };
    for (int i = 0; i < ITEM_COUNT; i++) {
        s_rows[i] = ui_theme_card(scr, 10, 62 + i * 40, 220, 34, i == s_selected);
        lv_obj_t *name = lv_label_create(s_rows[i]);
        lv_obj_set_style_text_font(name, &app_font_12, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(UI_INK), 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 4, 0);
        lv_label_set_text(name, NAMES[i]);
        s_values[i] = lv_label_create(s_rows[i]);
        lv_obj_set_style_text_font(s_values[i], &app_font_12, 0);
        lv_obj_set_style_text_color(s_values[i], lv_color_hex(UI_ACCENT), 0);
        lv_obj_align(s_values[i], LV_ALIGN_RIGHT_MID, -4, 0);
    }

    // 子视图(作息调整 / 连接手机,同一容器按需填充)。
    s_sub_view = lv_obj_create(scr);
    lv_obj_set_pos(s_sub_view, 0, 26);
    lv_obj_set_size(s_sub_view, 240, 288);
    lv_obj_set_style_bg_color(s_sub_view, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(s_sub_view, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_sub_view, 0, 0);
    lv_obj_set_style_radius(s_sub_view, 0, 0);
    lv_obj_clear_flag(s_sub_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_sub_view, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 2; i++) {
        s_rest_labels[i] = lv_label_create(s_sub_view);
        lv_obj_set_style_text_font(s_rest_labels[i], &app_font_24, 0);
        lv_obj_set_style_text_color(s_rest_labels[i], lv_color_hex(UI_INK), 0);
        lv_obj_set_pos(s_rest_labels[i], 24, 60 + i * 60);
    }
    lv_obj_t *rest_hint = lv_label_create(s_sub_view);
    lv_obj_set_style_text_font(rest_hint, &app_font_12, 0);
    lv_obj_set_style_text_color(rest_hint, lv_color_hex(UI_INK_DIM), 0);
    lv_label_set_text(rest_hint, "OK +30分 · 长按返回");
    lv_obj_set_pos(rest_hint, 24, 200);

    // 连接手机:二维码 + 服务地址 + 令牌状态。
    s_qr = NULL;
#if LV_USE_QRCODE
    s_qr = lv_qrcode_create(s_sub_view);
    lv_qrcode_set_size(s_qr, 120);
    lv_qrcode_set_dark_color(s_qr, lv_color_hex(0x0E1626));
    lv_qrcode_set_light_color(s_qr, lv_color_hex(0xFFFFFF));
    lv_obj_set_pos(s_qr, 60, 46);
#endif
    s_url_label = lv_label_create(s_sub_view);
    lv_obj_set_style_text_font(s_url_label, &app_font_12, 0);
    lv_obj_set_style_text_color(s_url_label, lv_color_hex(UI_INK), 0);
    lv_obj_set_pos(s_url_label, 0, 180);
    lv_obj_set_width(s_url_label, 240);
    lv_obj_set_style_text_align(s_url_label, LV_TEXT_ALIGN_CENTER, 0);
    s_token_label = lv_label_create(s_sub_view);
    lv_obj_set_style_text_font(s_token_label, &app_font_12, 0);
    lv_obj_set_style_text_color(s_token_label, lv_color_hex(UI_INK_DIM), 0);
    lv_obj_set_pos(s_token_label, 0, 204);
    lv_obj_set_width(s_token_label, 240);
    lv_obj_set_style_text_align(s_token_label, LV_TEXT_ALIGN_CENTER, 0);

    ui_settings_refresh();
    return s_settings;
}

lv_obj_t *ui_settings_screen(void) {
    return s_settings ? s_settings->screen : NULL;
}

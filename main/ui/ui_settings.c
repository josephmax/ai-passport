// main/ui/ui_settings.c —— 设置页实现(规格 §7/§7.1 严格分层)。
// 一级=大按钮(上下留给切页);二级=列表(上下=光标,OK=选中);
// 数值项进入调整态(上下=调值实时生效,OK=确认,长按=取消回滚);
// 流程项(作息/连接手机/配网)进入各自界面;长按逐级返回。
#include "ui_settings.h"

#include "app_prov.h"
#include "app_runtime.h"
#include "app_store.h"
#include "app_sync.h"
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
    ITEM_WIFI,
    ITEM_CONNECT,
    ITEM_SYNC,
    ITEM_COUNT,
} item_t;

typedef enum {
    SET_TOP = 0,     // 一级:大设置按钮
    SET_LIST,        // 二级:列表
    SET_ADJUST,      // 调整态:数值项被选中
    SET_REST,        // 流程:作息起止
    SET_CONNECT,     // 流程:连接手机(QR)
    SET_PROV,        // 流程:配网
} set_mode_t;

static ui_settings_t *s_settings;
static ui_status_bar_t s_status;
static lv_obj_t *s_top_view;
static lv_obj_t *s_list_view;
static lv_obj_t *s_sub_view;
static lv_obj_t *s_rows[ITEM_COUNT];
static lv_obj_t *s_values[ITEM_COUNT];
static int s_selected;
static set_mode_t s_mode;

// 调整态:进入时的值快照,长按取消时回滚。
static struct {
    uint8_t brightness;
    uint8_t volume;
    uint16_t auto_off_s;
} s_adjust_entry;

static lv_obj_t *s_rest_labels[2];
static int s_rest_pick;
static lv_obj_t *s_qr;
static lv_obj_t *s_url_label;
static lv_obj_t *s_token_label;
static lv_obj_t *s_hint_label;
static ui_page_dots_t s_dots;

static const uint16_t AUTO_OFF_STEPS[] = { 15, 30, 60, 120 };
#define AUTO_OFF_STEP_COUNT 4

static void save_and_apply(void) {
    app_runtime_t *rt = app_runtime();
    rt->rest.start_min = rt->settings.rest_start_min;
    rt->rest.end_min = rt->settings.rest_end_min;
    app_store_save_settings(&rt->settings);
}

static int auto_off_step_index(uint16_t s) {
    for (int i = 0; i < AUTO_OFF_STEP_COUNT; i++) {
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
    snprintf(buf, sizeof(buf), "%d分", rt->settings.auto_off_s / 60);
    lv_label_set_text(s_values[ITEM_SCREEN_OFF], buf);
    snprintf(buf, sizeof(buf), "%02d:%02d-%02d:%02d",
             rt->settings.rest_start_min / 60, rt->settings.rest_start_min % 60,
             rt->settings.rest_end_min / 60, rt->settings.rest_end_min % 60);
    lv_label_set_text(s_values[ITEM_REST], buf);
    switch (app_prov_state()) {
    case APP_PROV_AP_UP: lv_label_set_text(s_values[ITEM_WIFI], "热点已开"); break;
    case APP_PROV_CONNECTING: lv_label_set_text(s_values[ITEM_WIFI], "连接中"); break;
    case APP_PROV_PAIRING: lv_label_set_text(s_values[ITEM_WIFI], "配对中"); break;
    case APP_PROV_DONE: lv_label_set_text(s_values[ITEM_WIFI], "成功"); break;
    case APP_PROV_FAILED: lv_label_set_text(s_values[ITEM_WIFI], "重试"); break;
    default:
        lv_label_set_text(s_values[ITEM_WIFI], rt->wifi_connected ? "已连接" : "进入");
        break;
    }
    lv_label_set_text(s_values[ITEM_CONNECT], rt->paired ? "已配对" : "未配网");
    lv_label_set_text(s_values[ITEM_SYNC], rt->wifi_connected ? "按 OK 同步" : "未连接");
}

static void set_mode(set_mode_t mode) {
    s_mode = mode;
    lv_obj_add_flag(s_top_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_sub_view, LV_OBJ_FLAG_HIDDEN);
    switch (mode) {
    case SET_TOP: lv_obj_clear_flag(s_top_view, LV_OBJ_FLAG_HIDDEN); break;
    case SET_LIST:
    case SET_ADJUST: lv_obj_clear_flag(s_list_view, LV_OBJ_FLAG_HIDDEN); break;
    default: lv_obj_clear_flag(s_sub_view, LV_OBJ_FLAG_HIDDEN); break;
    }
    ui_settings_refresh();
}

void ui_settings_refresh(void) {
    if (!s_settings) return;
    ui_theme_status_bar_refresh(&s_status);

    for (int i = 0; i < ITEM_COUNT; i++) {
        bool cursor = s_mode == SET_LIST && i == s_selected;
        bool editing = s_mode == SET_ADJUST && i == s_selected;
        lv_obj_set_style_bg_color(s_rows[i],
            lv_color_hex(cursor || editing ? UI_SURFACE_HI : UI_SURFACE), 0);
        lv_obj_set_style_border_color(s_rows[i],
            lv_color_hex(cursor ? UI_ACCENT : (editing ? UI_WARN : UI_LINE)), 0);
        lv_obj_set_style_text_color(s_values[i],
            lv_color_hex(editing ? UI_WARN : UI_ACCENT), 0);
    }
    refresh_values();

    switch (s_mode) {
    case SET_TOP: ui_theme_hint_set(s_hint_label, "切页", "进入", NULL); break;
    case SET_LIST: ui_theme_hint_set(s_hint_label, "选择", "选中", "返回"); break;
    case SET_ADJUST: ui_theme_hint_set(s_hint_label, "调整", "确认", "取消"); break;
    case SET_REST: ui_theme_hint_set(s_hint_label, "起/止", "+30分", "返回"); break;
    case SET_CONNECT: ui_theme_hint_set(s_hint_label, NULL, NULL, "返回"); break;
    case SET_PROV: ui_theme_hint_set(s_hint_label, NULL, NULL, "退出配网"); break;
    default: break;
    }
}

// ---- 调整态:上下调值实时生效,OK 确认,长按回滚 ----
static void adjust_enter(void) {
    app_runtime_t *rt = app_runtime();
    s_adjust_entry.brightness = rt->settings.brightness;
    s_adjust_entry.volume = rt->settings.volume;
    s_adjust_entry.auto_off_s = rt->settings.auto_off_s;
    set_mode(SET_ADJUST);
}

static void adjust_apply(int dir) {
    app_runtime_t *rt = app_runtime();
    switch ((item_t)s_selected) {
    case ITEM_BRIGHTNESS: {
        int v = rt->settings.brightness + dir;
        if (v >= 1 && v <= 5) {
            rt->settings.brightness = (uint8_t)v;
            ui_theme_brightness_apply(rt->settings.brightness);
        }
        break;
    }
    case ITEM_VOLUME: {
        int v = rt->settings.volume + dir;
        if (v >= 0 && v <= 5) rt->settings.volume = (uint8_t)v;
        break;
    }
    case ITEM_SCREEN_OFF: {
        int idx = auto_off_step_index(rt->settings.auto_off_s) + dir;
        if (idx >= 0 && idx < AUTO_OFF_STEP_COUNT) {
            rt->settings.auto_off_s = AUTO_OFF_STEPS[idx];
        }
        break;
    }
    default: break;
    }
    refresh_values();
}

static void adjust_confirm(void) {
    save_and_apply();
    set_mode(SET_LIST);
}

static void adjust_cancel(void) {
    app_runtime_t *rt = app_runtime();
    rt->settings.brightness = s_adjust_entry.brightness;
    rt->settings.volume = s_adjust_entry.volume;
    rt->settings.auto_off_s = s_adjust_entry.auto_off_s;
    ui_theme_brightness_apply(rt->settings.brightness);
    set_mode(SET_LIST);
}

// ---- 流程页内容刷新 ----
static void rest_refresh(void) {
    app_runtime_t *rt = app_runtime();
    for (int i = 0; i < 2; i++) {
        uint16_t m = i == 0 ? rt->settings.rest_start_min
                            : rt->settings.rest_end_min;
        lv_label_set_text_fmt(s_rest_labels[i], "%s %02d:%02d",
                              i == 0 ? "起始" : "结束", m / 60, m % 60);
        lv_obj_set_style_text_color(s_rest_labels[i],
            lv_color_hex(i == s_rest_pick ? UI_ACCENT : UI_INK), 0);
    }
}

static void connect_refresh(void) {
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

static void prov_refresh(void) {
    const char *ap = app_prov_ap_name();
    lv_label_set_text(s_rest_labels[0], ap ? ap : "Passport-****");
    switch (app_prov_state()) {
    case APP_PROV_AP_UP: lv_label_set_text(s_rest_labels[1], "热点已开·手机连接后弹出"); break;
    case APP_PROV_CONNECTING: lv_label_set_text(s_rest_labels[1], "正在连接 Wi-Fi"); break;
    case APP_PROV_PAIRING: lv_label_set_text(s_rest_labels[1], "正在配对"); break;
    case APP_PROV_DONE: lv_label_set_text(s_rest_labels[1], "配网完成"); break;
    case APP_PROV_FAILED: lv_label_set_text(s_rest_labels[1], "失败·OK 重试"); break;
    default: lv_label_set_text(s_rest_labels[1], "启动中"); break;
    }
}

bool ui_settings_key(bool ok_short, bool ok_long, bool up, bool down) {
    if (!s_settings) return false;
    app_runtime_t *rt = app_runtime();
    int dir = up ? 1 : (down ? -1 : 0);

    switch (s_mode) {
    case SET_TOP:
        if (ok_short) {
            set_mode(SET_LIST);
            return true;
        }
        return false;   // 上下留给 Shell 切页;长按无操作

    case SET_LIST:
        if (ok_long) {
            set_mode(SET_TOP);
            return true;
        }
        if (up || down) {
            s_selected = (s_selected + (up ? ITEM_COUNT - 1 : 1)) % ITEM_COUNT;
            ui_settings_refresh();
            return true;
        }
        if (!ok_short) return false;
        switch ((item_t)s_selected) {
        case ITEM_BRIGHTNESS:
        case ITEM_VOLUME:
        case ITEM_SCREEN_OFF:
            adjust_enter();
            break;
        case ITEM_REST:
            s_rest_pick = 0;
            set_mode(SET_REST);
            rest_refresh();
            break;
        case ITEM_WIFI:
            app_prov_request_start();   // 异步:Wi-Fi 重配绝不占 UI 锁
            set_mode(SET_PROV);
            prov_refresh();
            break;
        case ITEM_CONNECT:
            set_mode(SET_CONNECT);
            connect_refresh();
            break;
        case ITEM_SYNC:
            if (rt->paired && rt->wifi_connected) {
                app_sync_request_now();
                lv_label_set_text(s_values[ITEM_SYNC], "同步中…");
            }
            break;
        default:
            break;
        }
        return true;

    case SET_ADJUST:
        if (ok_long) {
            adjust_cancel();
        } else if (ok_short) {
            adjust_confirm();
        } else if (dir) {
            adjust_apply(dir);
        }
        return true;

    case SET_REST:
        if (ok_long) {
            save_and_apply();
            set_mode(SET_LIST);
        } else if (up || down) {
            s_rest_pick ^= 1;
            rest_refresh();
        } else if (ok_short) {
            uint16_t *m = s_rest_pick == 0 ? &rt->settings.rest_start_min
                                           : &rt->settings.rest_end_min;
            *m = (uint16_t)((*m + 30) % 1440);
            rest_refresh();
        }
        return true;

    case SET_CONNECT:
        if (ok_long) set_mode(SET_LIST);
        return true;

    case SET_PROV:
        if (ok_long) {
            app_prov_cancel();
            set_mode(SET_LIST);
        } else if (ok_short && app_prov_state() == APP_PROV_FAILED) {
            app_prov_request_start();   // 失败重试
            prov_refresh();
        }
        return true;

    default:
        return false;
    }
}

ui_settings_t *ui_settings_create(void) {
    if (s_settings) return s_settings;
    lv_obj_t *scr = ui_theme_screen();
    s_settings = lv_malloc(sizeof(ui_settings_t));
    s_settings->screen = scr;
    ui_theme_status_bar_create(scr, &s_status);
    s_hint_label = ui_theme_hint_create(scr);
    ui_theme_page_dots_create(scr, &s_dots);

    // ---- 一级:一个大设置按钮 ----
    s_top_view = lv_obj_create(scr);
    lv_obj_set_pos(s_top_view, 0, 26);
    lv_obj_set_size(s_top_view, 240, 276);
    lv_obj_set_style_bg_opa(s_top_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_top_view, 0, 0);
    lv_obj_set_style_pad_all(s_top_view, 0, 0);
    lv_obj_clear_flag(s_top_view, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *big = ui_theme_card(s_top_view, 40, 96, 160, 72, true);
    lv_obj_set_style_radius(big, 16, 0);
    lv_obj_t *big_label = lv_label_create(big);
    lv_obj_set_style_text_font(big_label, &app_font_24, 0);
    lv_obj_set_style_text_color(big_label, lv_color_hex(UI_INK), 0);
    lv_label_set_text(big_label, "设 置");
    lv_obj_align(big_label, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_t *big_sub = lv_label_create(big);
    lv_obj_set_style_text_font(big_sub, &app_font_12, 0);
    lv_obj_set_style_text_color(big_sub, lv_color_hex(UI_INK_DIM), 0);
    lv_label_set_text(big_sub, "OK 进入");
    lv_obj_align(big_sub, LV_ALIGN_BOTTOM_MID, 0, -6);

    // ---- 二级:设置列表 ----
    s_list_view = lv_obj_create(scr);
    lv_obj_set_pos(s_list_view, 0, 26);
    lv_obj_set_size(s_list_view, 240, 276);
    lv_obj_set_style_bg_opa(s_list_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list_view, 0, 0);
    lv_obj_set_style_pad_all(s_list_view, 0, 0);
    lv_obj_clear_flag(s_list_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_list_view, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(s_list_view);
    lv_obj_set_style_text_font(title, &app_font_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_INK), 0);
    lv_label_set_text(title, "设置");
    lv_obj_set_pos(title, 12, 0);

    static const char *NAMES[ITEM_COUNT] = {
        "亮度", "音量", "自动息屏", "作息时间", "Wi-Fi 配网", "连接手机", "立即同步",
    };
    for (int i = 0; i < ITEM_COUNT; i++) {
        s_rows[i] = ui_theme_card(s_list_view, 10, 34 + i * 34, 220, 30, i == s_selected);
        lv_obj_set_style_pad_all(s_rows[i], 6, 0);
        lv_obj_t *name = lv_label_create(s_rows[i]);
        lv_obj_set_style_text_font(name, &app_font_12, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(UI_INK), 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
        lv_label_set_text(name, NAMES[i]);
        s_values[i] = lv_label_create(s_rows[i]);
        lv_obj_set_style_text_font(s_values[i], &app_font_12, 0);
        lv_obj_set_style_text_color(s_values[i], lv_color_hex(UI_ACCENT), 0);
        lv_obj_align(s_values[i], LV_ALIGN_RIGHT_MID, 0, 0);
    }

    // ---- 流程页容器(作息/连接/配网共用,按模式填充) ----
    s_sub_view = lv_obj_create(scr);
    lv_obj_set_pos(s_sub_view, 0, 26);
    lv_obj_set_size(s_sub_view, 240, 276);
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
        lv_obj_set_pos(s_rest_labels[i], 24, 40 + i * 56);
    }
    s_qr = NULL;
#if LV_USE_QRCODE
    s_qr = lv_qrcode_create(s_sub_view);
    lv_qrcode_set_size(s_qr, 110);
    lv_qrcode_set_dark_color(s_qr, lv_color_hex(0x0E1626));
    lv_qrcode_set_light_color(s_qr, lv_color_hex(0xFFFFFF));
    lv_obj_set_pos(s_qr, 65, 30);
#endif
    s_url_label = lv_label_create(s_sub_view);
    lv_obj_set_style_text_font(s_url_label, &app_font_12, 0);
    lv_obj_set_style_text_color(s_url_label, lv_color_hex(UI_INK), 0);
    lv_obj_set_pos(s_url_label, 0, 158);
    lv_obj_set_width(s_url_label, 240);
    lv_obj_set_style_text_align(s_url_label, LV_TEXT_ALIGN_CENTER, 0);
    s_token_label = lv_label_create(s_sub_view);
    lv_obj_set_style_text_font(s_token_label, &app_font_12, 0);
    lv_obj_set_style_text_color(s_token_label, lv_color_hex(UI_INK_DIM), 0);
    lv_obj_set_pos(s_token_label, 0, 180);
    lv_obj_set_width(s_token_label, 240);
    lv_obj_set_style_text_align(s_token_label, LV_TEXT_ALIGN_CENTER, 0);

    set_mode(SET_TOP);
    return s_settings;
}

lv_obj_t *ui_settings_screen(void) {
    return s_settings ? s_settings->screen : NULL;
}

ui_page_dots_t *ui_settings_dots(void) {
    return &s_dots;
}

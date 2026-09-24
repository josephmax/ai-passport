// Usage list -> quota detail. There is only one active content tree per screen.
#include "ui_dash.h"
#include "app_fmt.h"
#include "app_runtime.h"
#include "app_snapshot.h"
#include "ui_theme.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

enum { WEEKLY, ROLLING, TOKENS, DIM_COUNT };
static const char *TITLES[] = {"周额度", "5 小时额度", "本周 Token"};
static ui_dash_t *s_dash;
static ui_status_bar_t s_status;
static lv_obj_t *s_content, *s_hint, *s_title, *s_account;
static lv_obj_t *s_cards[3], *s_big[3], *s_sub[3], *s_bars[3];
static lv_obj_t *s_detail_big, *s_detail_sub, *s_detail_reset, *s_detail_bar;
static lv_obj_t *s_detail_rows[APP_SNAPSHOT_MAX_ACCOUNTS - 1];
static int s_selected;
static bool s_drill;

static const char *provider_label(const char *provider) {
    if (!strcmp(provider, "claude")) return "Claude";
    if (!strcmp(provider, "glm")) return "GLM";
    if (!strcmp(provider, "deepseek")) return "DeepSeek";
    if (!strcmp(provider, "chatgpt")) return "ChatGPT";
    return "Coding"; // Remote names are not guaranteed to be in the font subset.
}

static const app_quota_t *quota(const app_account_t *account, int dim) {
    if (!account) return NULL;
    if (dim == WEEKLY) return account->has_weekly ? &account->weekly : NULL;
    if (dim == ROLLING) return account->has_rolling5h ? &account->rolling5h : NULL;
    return account->has_weekly_tokens ? &account->weekly_tokens : NULL;
}

static void number(const app_quota_t *q, char *buf, size_t cap) {
    int pm;
    if (!q) snprintf(buf, cap, "--");
    else if (app_snapshot_permille(q, &pm)) snprintf(buf, cap, "%d%%", pm / 10);
    else if (!strcmp(q->unit, "CNY")) snprintf(buf, cap, "%.2f", q->used);
    else app_fmt_daily_tokens(q->used, buf, cap);
}

static void reset_text(const app_quota_t *q, char *buf, size_t cap) {
    int64_t reset;
    if (!q) snprintf(buf, cap, "暂无数据");
    else if (!strcmp(q->unit, "CNY")) snprintf(buf, cap, "余额 CNY");
    else if (q->reset_at[0] && app_snapshot_parse_iso8601(q->reset_at, &reset)) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        int64_t left = reset - tv.tv_sec;
        if (left <= 0) snprintf(buf, cap, "等待重置");
        else if (left >= 86400) snprintf(buf, cap, "%lld 天后重置", (long long)((left + 86399) / 86400));
        else snprintf(buf, cap, "剩余 %lldh %lldm", (long long)(left / 3600), (long long)(left / 60 % 60));
    } else snprintf(buf, cap, "%s", q->has_cap ? "用量 / 上限" : "未设预算");
}

static lv_obj_t *meter(lv_obj_t *parent, int x, int y, int w) {
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, 4);
    lv_bar_set_range(bar, 0, 1000);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_LINE), 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_ACCENT), LV_PART_INDICATOR);
    return bar;
}

static void refresh_bar(lv_obj_t *bar, const app_quota_t *q) {
    int pm = 0;
    if (!q || !app_snapshot_permille(q, &pm)) lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    else {
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(bar, pm, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(pm >= 900 ? UI_WARN : UI_ACCENT), LV_PART_INDICATOR);
    }
}

void ui_dash_refresh(void) {
    if (!s_dash) return;
    ui_theme_status_bar_refresh(&s_status);
    app_runtime_t *rt = app_runtime();
    const app_account_t *primary = rt->snap_valid && rt->snap.account_count ? &rt->snap.accounts[0] : NULL;
    char buf[96];
    snprintf(buf, sizeof(buf), "%s · 主力", primary ? provider_label(primary->provider) : "--");
    lv_label_set_text(s_account, buf);
    if (!s_drill) {
        for (int i = 0; i < DIM_COUNT; i++) {
            const app_quota_t *q = quota(primary, i);
            ui_theme_select(s_cards[i], i == s_selected);
            number(q, buf, sizeof(buf)); lv_label_set_text(s_big[i], buf);
            reset_text(q, buf, sizeof(buf)); lv_label_set_text(s_sub[i], buf);
            refresh_bar(s_bars[i], q);
        }
    } else {
        lv_label_set_text(s_title, TITLES[s_selected]);
        const app_quota_t *q = quota(primary, s_selected);
        number(q, buf, sizeof(buf)); lv_label_set_text(s_detail_big, buf);
        refresh_bar(s_detail_bar, q);
        if (q) {
            char used[24], cap[24];
            app_fmt_daily_tokens(q->used, used, sizeof(used));
            if (q->has_cap) {
                app_fmt_daily_tokens(q->cap, cap, sizeof(cap));
                snprintf(buf, sizeof(buf), "已用 %s / %s %s", used, cap, q->unit);
            } else snprintf(buf, sizeof(buf), "%s %s", used, q->unit);
        } else snprintf(buf, sizeof(buf), "主力账户暂无数据");
        lv_label_set_text(s_detail_sub, buf);
        reset_text(q, buf, sizeof(buf)); lv_label_set_text(s_detail_reset, buf);
        for (int i = 0; i < APP_SNAPSHOT_MAX_ACCOUNTS - 1; i++) {
            if (rt->snap_valid && i + 1 < rt->snap.account_count) {
                const app_account_t *a = &rt->snap.accounts[i + 1];
                const app_quota_t *other = quota(a, s_selected);
                char val[24]; number(other, val, sizeof(val));
                snprintf(buf, sizeof(buf), "%s   %s%s", provider_label(a->provider), val,
                    other && !strcmp(other->unit, "CNY") ? " CNY" : "");
                lv_label_set_text(s_detail_rows[i], buf);
                lv_obj_clear_flag(s_detail_rows[i], LV_OBJ_FLAG_HIDDEN);
            } else lv_obj_add_flag(s_detail_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void show(bool drill) {
    if (s_content) lv_obj_delete(s_content);
    s_content = ui_theme_box(s_dash->screen, 0, 24, 240, 274, UI_BG);
    s_drill = drill;
    s_title = ui_theme_label(s_content, 12, 6, 216, &app_font_24, UI_INK, drill ? TITLES[s_selected] : "用量");
    if (!drill) {
        s_account = ui_theme_label(s_content, 115, 16, 113, &app_font_12, UI_INK_DIM, "");
        lv_obj_set_style_text_align(s_account, LV_TEXT_ALIGN_RIGHT, 0);
        for (int i = 0; i < DIM_COUNT; i++) {
            s_cards[i] = ui_theme_card(s_content, 8, 37 + i * 78, 224, 72, false);
            ui_theme_label(s_cards[i], 10, 8, 204, &app_font_12, UI_INK_DIM, TITLES[i]);
            s_big[i] = ui_theme_label(s_cards[i], 10, 24, 204, &lv_font_montserrat_28, UI_INK, "--");
            // Context below the number avoids overlap with long four-digit values.
            s_sub[i] = ui_theme_label(s_cards[i], 116, 37, 98, &app_font_12, UI_INK_DIM, "");
            lv_obj_set_style_text_align(s_sub[i], LV_TEXT_ALIGN_RIGHT, 0);
            s_bars[i] = meter(s_cards[i], 10, 59, 204);
        }
        ui_theme_hint_set(s_hint, "选择", "详情", "返回");
    } else {
        s_account = ui_theme_label(s_content, 14, 43, 212, &app_font_12, UI_INK_DIM, "");
        s_detail_big = ui_theme_label(s_content, 12, 65, 216, &lv_font_montserrat_32, UI_INK, "--");
        s_detail_bar = meter(s_content, 14, 107, 212);
        s_detail_sub = ui_theme_label(s_content, 14, 121, 212, &app_font_12, UI_INK_DIM, "");
        s_detail_reset = ui_theme_label(s_content, 14, 142, 212, &app_font_12, UI_ACCENT, "");
        ui_theme_label(s_content, 14, 164, 212, &app_font_12, UI_INK_DIM, "其他账户 · 同维度");
        for (int i = 0; i < APP_SNAPSHOT_MAX_ACCOUNTS - 1; i++)
            s_detail_rows[i] = ui_theme_label(s_content, 14, 184 + i * 17, 212, &app_font_12, UI_INK, "");
        ui_theme_hint_set(s_hint, "切换", NULL, "返回");
    }
    ui_dash_refresh();
}

bool ui_dash_key(bool ok_short, bool ok_long, bool up, bool down) {
    if (ok_long) { if (!s_drill) return false; show(false); }
    else if (up || down) {
        s_selected = (s_selected + (up ? DIM_COUNT - 1 : 1)) % DIM_COUNT;
        ui_dash_refresh();
    } else if (ok_short && !s_drill) show(true);
    return true;
}

ui_dash_t *ui_dash_create(void) {
    if (s_dash) return s_dash;
    s_dash = lv_malloc(sizeof(*s_dash));
    s_dash->screen = ui_theme_screen();
    ui_theme_status_bar_create(s_dash->screen, &s_status);
    s_hint = ui_theme_hint_create(s_dash->screen);
    s_selected = WEEKLY;
    show(false);
    return s_dash;
}

void ui_dash_destroy(void) {
    if (!s_dash) return;
    lv_obj_delete(s_dash->screen);
    lv_free(s_dash);
    s_dash = NULL;
    s_content = NULL;
}

lv_obj_t *ui_dash_screen(void) { return s_dash ? s_dash->screen : NULL; }

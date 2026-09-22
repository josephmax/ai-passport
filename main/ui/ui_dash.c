// main/ui/ui_dash.c —— 仪表盘页实现:三张汇总卡(竖排) + 下钻详情。
//
// 键位解析(规格 §3 定稿 + §4):主界面 UP/DOWN 切页、OK 下钻当前高亮卡;
// 高亮卡即上次下钻的维度(初始周额度)。下钻页 UP/DOWN 在三项详情间切换,
// OK 长按返回主界面 —— 由此主界面无需独立的选中移动键。
#include "ui_dash.h"

#include "app_fmt.h"
#include "app_runtime.h"
#include "app_snapshot.h"
#include "ui_theme.h"
// 提示栏文案由 ui_theme_hint_set 组装

#include <stdio.h>
#include <string.h>

typedef enum { DIM_WEEKLY = 0, DIM_ROLLING5H, DIM_TOKENS, DIM_COUNT } dim_t;

static const char *DIM_TITLES[DIM_COUNT] = { "周额度", "5小时额度", "本周Token" };

typedef struct {
    lv_obj_t *card;
    lv_obj_t *title;
    lv_obj_t *big;      // 百分比大字 / 纯数值
    lv_obj_t *sub;      // 用量/上限 + 重置/剩余
} card_widgets_t;

static ui_dash_t *s_dash;
static ui_status_bar_t s_status;
static lv_obj_t *s_hint;
static ui_page_dots_t s_dots;
static card_widgets_t s_cards[DIM_COUNT];
static lv_obj_t *s_main_view;
static lv_obj_t *s_drill_view;
static lv_obj_t *s_drill_title;
static lv_obj_t *s_drill_big;
static lv_obj_t *s_drill_sub;
static lv_obj_t *s_drill_rows[APP_SNAPSHOT_MAX_ACCOUNTS];
static dim_t s_selected;      // 主界面高亮 = 下钻目标
static dim_t s_drill_dim;     // 下钻页当前维度
static bool s_in_drill;

static const app_quota_t *quota_of(const app_account_t *acc, dim_t dim,
                                   bool *present) {
    switch (dim) {
    case DIM_WEEKLY: *present = acc->has_weekly; return &acc->weekly;
    case DIM_ROLLING5H: *present = acc->has_rolling5h; return &acc->rolling5h;
    default: *present = acc->has_weekly_tokens; return &acc->weekly_tokens;
    }
}

static const char *provider_label(const char *provider) {
    if (strcmp(provider, "claude") == 0) return "Claude";
    if (strcmp(provider, "glm") == 0) return "GLM";
    if (strcmp(provider, "deepseek") == 0) return "DeepSeek";
    if (strcmp(provider, "chatgpt") == 0) return "ChatGPT";
    return provider;
}

static void set_percent_big(lv_obj_t *label, const app_quota_t *q) {
    int pm = 0;
    char buf[16];
    if (app_snapshot_permille(q, &pm)) {
        snprintf(buf, sizeof(buf), "%d%%", pm / 10);
        lv_obj_set_style_text_color(label,
            lv_color_hex(pm >= 900 ? UI_WARN : UI_INK), 0);
    } else {
        app_fmt_tokens(q->used, buf, sizeof(buf));
        lv_obj_set_style_text_color(label, lv_color_hex(UI_INK), 0);
    }
    lv_label_set_text(label, buf);
}

// 主界面单卡:标题 + 大字 + 副行(主力账户)。
static void refresh_card(card_widgets_t *cw, dim_t dim, bool selected) {
    app_runtime_t *rt = app_runtime();
    lv_obj_set_style_border_color(cw->card,
        lv_color_hex(selected ? UI_ACCENT : UI_LINE), 0);
    lv_obj_set_style_bg_color(cw->card,
        lv_color_hex(selected ? UI_SURFACE_HI : UI_SURFACE), 0);

    lv_label_set_text(cw->title, DIM_TITLES[dim]);
    if (!rt->snap_valid || rt->snap.account_count == 0) {
        lv_label_set_text(cw->big, "--");
        lv_obj_set_style_text_color(cw->big, lv_color_hex(UI_INK_DIM), 0);
        lv_label_set_text(cw->sub, rt->paired ? "等待同步" : "未配网");
        return;
    }
    const app_account_t *main_acc = &rt->snap.accounts[0];
    bool present = false;
    const app_quota_t *q = quota_of(main_acc, dim, &present);
    if (!present) {
        lv_label_set_text(cw->big, "--");
        lv_obj_set_style_text_color(cw->big, lv_color_hex(UI_INK_DIM), 0);
        lv_label_set_text(cw->sub, "无数据");
        return;
    }
    set_percent_big(cw->big, q);
    char buf[48], v1[16], v2[16];
    app_fmt_tokens(q->used, v1, sizeof(v1));
    if (q->has_cap) {
        app_fmt_tokens(q->cap, v2, sizeof(v2));
        snprintf(buf, sizeof(buf), "%s/%s %s", v1, v2, q->unit);
    } else {
        snprintf(buf, sizeof(buf), "%s %s", v1, q->unit);
    }
    lv_label_set_text(cw->sub, buf);
}

static void refresh_drill(void) {
    app_runtime_t *rt = app_runtime();
    lv_label_set_text(s_drill_title, DIM_TITLES[s_drill_dim]);

    if (!rt->snap_valid || rt->snap.account_count == 0) {
        lv_label_set_text(s_drill_big, "--");
        lv_label_set_text(s_drill_sub, "长按返回");
        for (int i = 0; i < APP_SNAPSHOT_MAX_ACCOUNTS; i++) {
            lv_obj_add_flag(s_drill_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    const app_account_t *main_acc = &rt->snap.accounts[0];
    bool present = false;
    const app_quota_t *q = quota_of(main_acc, s_drill_dim, &present);
    if (present) {
        set_percent_big(s_drill_big, q);
        char buf[96], v1[16], v2[16];
        app_fmt_tokens(q->used, v1, sizeof(v1));
        if (q->has_cap) {
            app_fmt_tokens(q->cap, v2, sizeof(v2));
            snprintf(buf, sizeof(buf), "%s/%s %s · %s", v1, v2, q->unit,
                     q->reset_at[0] ? q->reset_at : "");
        } else {
            snprintf(buf, sizeof(buf), "%s %s", v1, q->unit);
        }
        lv_label_set_text(s_drill_sub, buf);
    } else {
        lv_label_set_text(s_drill_big, "--");
        lv_label_set_text(s_drill_sub, "主力账户无此维度");
    }

    for (int i = 0; i < APP_SNAPSHOT_MAX_ACCOUNTS; i++) {
        if (i < rt->snap.account_count && i > 0) {
            const app_account_t *acc = &rt->snap.accounts[i];
            bool has = false;
            const app_quota_t *aq = quota_of(acc, s_drill_dim, &has);
            char row[48];
            if (has) {
                int pm = 0;
                char val[16];
                if (app_snapshot_permille(aq, &pm)) {
                    snprintf(val, sizeof(val), "%d%%", pm / 10);
                } else {
                    app_fmt_tokens(aq->used, val, sizeof(val));
                }
                snprintf(row, sizeof(row), "%s  %s", provider_label(acc->provider), val);
            } else {
                snprintf(row, sizeof(row), "%s  --", provider_label(acc->provider));
            }
            lv_label_set_text(s_drill_rows[i - 1], row);
            lv_obj_clear_flag(s_drill_rows[i - 1], LV_OBJ_FLAG_HIDDEN);
        } else if (i > 0) {
            lv_obj_add_flag(s_drill_rows[i - 1], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ui_dash_refresh(void) {
    if (!s_dash) return;
    ui_theme_status_bar_refresh(&s_status);
    ui_theme_hint_set(s_hint,
                      s_in_drill ? "切换维度" : "切页",
                      s_in_drill ? NULL : "下钻",
                      s_in_drill ? "返回" : NULL);
    if (s_in_drill) {
        refresh_drill();
    } else {
        for (int i = 0; i < DIM_COUNT; i++) {
            refresh_card(&s_cards[i], (dim_t)i, i == (int)s_selected);
        }
    }
}

void ui_dash_destroy(void) {
    if (!s_dash) return;
    lv_obj_delete(s_dash->screen);
    lv_free(s_dash);
    s_dash = NULL;
    memset(s_cards, 0, sizeof(s_cards));
    s_in_drill = false;
    s_selected = DIM_WEEKLY;
}

static void enter_drill(dim_t dim) {
    s_drill_dim = dim;
    s_selected = dim;
    s_in_drill = true;
    lv_obj_add_flag(s_main_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_drill_view, LV_OBJ_FLAG_HIDDEN);
    refresh_drill();
}

static void exit_drill(void) {
    s_in_drill = false;
    lv_obj_clear_flag(s_main_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_drill_view, LV_OBJ_FLAG_HIDDEN);
    ui_dash_refresh();
}

bool ui_dash_key(bool ok_short, bool ok_long, bool up, bool down) {
    if (!s_dash) return false;
    if (!s_in_drill) {
        if (ok_short) {
            enter_drill(s_selected);
            return true;
        }
        return false;
    }
    if (ok_long) {
        exit_drill();
        return true;
    }
    if (up || down) {
        s_drill_dim = (dim_t)((s_drill_dim + (up ? DIM_COUNT - 1 : 1)) % DIM_COUNT);
        refresh_drill();
        return true;
    }
    return true;   // 下钻页 OK 短按无操作,但事件到此为止
}

ui_dash_t *ui_dash_create(void) {
    if (s_dash) return s_dash;
    lv_obj_t *scr = ui_theme_screen();
    s_dash = lv_malloc(sizeof(ui_dash_t));
    s_dash->screen = scr;

    ui_theme_status_bar_create(scr, &s_status);
    s_hint = ui_theme_hint_create(scr);
    ui_theme_page_dots_create(scr, &s_dots);

    // 主界面:三卡竖排,各占约 1/3 屏(标题行下方 30..296)。
    s_main_view = lv_obj_create(scr);
    lv_obj_set_pos(s_main_view, 0, 26);
    lv_obj_set_size(s_main_view, 240, 270);
    lv_obj_set_style_bg_opa(s_main_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_main_view, 0, 0);
    lv_obj_set_style_pad_all(s_main_view, 0, 0);
    lv_obj_clear_flag(s_main_view, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < DIM_COUNT; i++) {
        int y = 2 + i * 89;
        s_cards[i].card = ui_theme_card(s_main_view, 10, y, 220, 84, i == (int)s_selected);
        s_cards[i].title = lv_label_create(s_cards[i].card);
        lv_obj_set_style_text_font(s_cards[i].title, &app_font_12, 0);
        lv_obj_set_style_text_color(s_cards[i].title, lv_color_hex(UI_INK_DIM), 0);
        lv_obj_align(s_cards[i].title, LV_ALIGN_TOP_LEFT, 2, 0);
        s_cards[i].big = lv_label_create(s_cards[i].card);
        lv_obj_set_style_text_font(s_cards[i].big, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(s_cards[i].big, lv_color_hex(UI_INK), 0);
        lv_obj_align(s_cards[i].big, LV_ALIGN_TOP_RIGHT, -2, 14);
        s_cards[i].sub = lv_label_create(s_cards[i].card);
        lv_obj_set_style_text_font(s_cards[i].sub, &app_font_12, 0);
        lv_obj_set_style_text_color(s_cards[i].sub, lv_color_hex(UI_INK_DIM), 0);
        lv_obj_align(s_cards[i].sub, LV_ALIGN_BOTTOM_LEFT, 2, 0);
    }

    // 下钻页:标题 + 主力账户明细 + 其余账户行列表。
    s_drill_view = lv_obj_create(scr);
    lv_obj_set_pos(s_drill_view, 0, 26);
    lv_obj_set_size(s_drill_view, 240, 270);
    lv_obj_set_style_bg_opa(s_drill_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_drill_view, 0, 0);
    lv_obj_set_style_pad_all(s_drill_view, 0, 0);
    lv_obj_clear_flag(s_drill_view, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = ui_theme_card(s_drill_view, 10, 2, 220, 104, true);
    s_drill_title = lv_label_create(panel);
    lv_obj_set_style_text_font(s_drill_title, &app_font_24, 0);
    lv_obj_set_style_text_color(s_drill_title, lv_color_hex(UI_ACCENT), 0);
    lv_obj_align(s_drill_title, LV_ALIGN_TOP_LEFT, 2, 0);
    s_drill_big = lv_label_create(panel);
    lv_obj_set_style_text_font(s_drill_big, &lv_font_montserrat_28, 0);
    lv_obj_align(s_drill_big, LV_ALIGN_TOP_RIGHT, -2, 26);
    s_drill_sub = lv_label_create(panel);
    lv_obj_set_style_text_font(s_drill_sub, &app_font_12, 0);
    lv_obj_set_style_text_color(s_drill_sub, lv_color_hex(UI_INK_DIM), 0);
    lv_obj_align(s_drill_sub, LV_ALIGN_BOTTOM_LEFT, 2, 0);

    for (int i = 0; i < APP_SNAPSHOT_MAX_ACCOUNTS - 1; i++) {
        lv_obj_t *row = lv_obj_create(s_drill_view);
        lv_obj_set_pos(row, 10, 114 + i * 34);
        lv_obj_set_size(row, 220, 30);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_SURFACE), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lab = lv_label_create(row);
        lv_obj_set_style_text_font(lab, &app_font_12, 0);
        lv_obj_set_style_text_color(lab, lv_color_hex(UI_INK), 0);
        lv_obj_align(lab, LV_ALIGN_LEFT_MID, 8, 0);
        s_drill_rows[i] = lab;
    }
    lv_obj_add_flag(s_drill_view, LV_OBJ_FLAG_HIDDEN);
    ui_dash_refresh();
    return s_dash;
}

lv_obj_t *ui_dash_screen(void) {
    return s_dash ? s_dash->screen : NULL;
}

ui_page_dots_t *ui_dash_dots(void) {
    return &s_dots;
}

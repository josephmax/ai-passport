// main/ui/ui_pet.c —— 宠物页实现。
//
// 内存策略(无 PSRAM 红线):地图条带 240×160×2=75K 单份堆缓冲,两个
// lv_image 共享同一数据源实现无缝循环;动作帧只驻留当前动作
// (切换时释放重载);天气/装扮小精灵按需加载。
#include "ui_pet.h"

#include "app_assets.h"
#include "app_fmt.h"
#include "app_home.h"
#include "app_focus.h"
#include "app_runtime.h"
#include "app_time.h"
#include "app_xp.h"
#include "esp_log.h"
#include "ui_theme.h"

#if __has_include("app_identity_local.h")
#include "app_identity_local.h"
#endif
#ifndef APP_BADGE_DEFAULT_NAME
#define APP_BADGE_DEFAULT_NAME "你的名字"
#endif
#ifndef APP_BADGE_DEFAULT_ROLE
#define APP_BADGE_DEFAULT_ROLE ""
#endif

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "ui_pet";

#define MAP_W 240
#define MAP_H 160
#define MAP_Y 86
#define PET_X 84            // 左侧角色给地图上的番茄进度留出空间
#define PET_SCALE 512       // 2x 最近邻放大:64x64 素材显示为 128x128,
                            // 像素风方块感;零额外内存(素材不变,仅显示变换)
#define GROUND_Y (MAP_Y + MAP_H - 2)
#define SCROLL_PX_PER_S 30
#define WEATHER_SPRITES 3
#define TICK_MS 33

typedef enum { ACT_RUN = 0, ACT_FIGHT, ACT_SLEEP, ACT_VICTORY, ACT_NONE } act_t;

static ui_pet_t *s_pet;
static ui_status_bar_t s_status;
static lv_obj_t *s_hint;
static app_home_t s_home;
static lv_obj_t *s_head, *s_head_title, *s_head_value, *s_head_note, *s_badge_name, *s_badge_role;
static lv_obj_t *s_focus_box, *s_focus_title, *s_focus_value, *s_focus_count, *s_settings_box;
static lv_obj_t *s_dot_center;

// 地图:单份数据,两个 image 拼接循环。
static app_asset_frames_t s_map;
static lv_image_dsc_t s_map_dsc;
static lv_obj_t *s_map_img[2];
static int s_map_offset;
static int s_scroll_ms;   // 位移毫秒累积:30px/s * 33ms/拍 = 0.99px,
                          // 整数除法直接算每拍位移曾恒为 0 → 地图永远不滚

// 当前动作帧 + 每帧描述符(数据指针指向 s_act.data 内部)。
static app_asset_frames_t s_act;
static lv_image_dsc_t *s_act_dsc;
static act_t s_act_loaded;      // 已加载到 s_act 的动作
static lv_obj_t *s_pet_img;
static act_t s_act_current;
static uint32_t s_frame_ms;
static uint16_t s_frame_idx;

// 胜利动画窗口。
static bool s_victory_active;
static int64_t s_victory_until_ms;

// 天气叠加精灵(共享一组帧,多实例不同相位)。
static app_asset_frames_t s_wx;
static lv_obj_t *s_wx_img[WEATHER_SPRITES];
static int s_wx_x[WEATHER_SPRITES], s_wx_y[WEATHER_SPRITES];
static bool s_wx_loaded;

// 装扮元素(1 个,P1 固定)。
static app_asset_frames_t s_deco;
static lv_obj_t *s_deco_img;
static int s_deco_x;

// 时段调色层 + HUD。
static lv_obj_t *s_tint;
static lv_obj_t *s_lv_label;
static lv_obj_t *s_lv_bar;
static lv_obj_t *s_clock_label;
static lv_obj_t *s_xp_values[3];
static lv_obj_t *s_unit_dots[APP_FOCUS_MAX_UNITS];


static lv_timer_t *s_timer;
static bool s_paused;

static int64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// 精灵(RGB565A8):颜色平面 w*h*2 + A8 透明平面 w*h;地图(RGB565)不透明。
#define SPRITE_BPP 3   // 字节/像素(RGB565A8)

static void fill_dsc(lv_image_dsc_t *dsc, const uint8_t *data,
                     uint16_t w, uint16_t h, bool alpha) {
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.stride = w * 2;   // 颜色平面步长
    dsc->header.cf = alpha ? LV_COLOR_FORMAT_RGB565A8 : LV_COLOR_FORMAT_RGB565;
    // data_size 缺失曾让全部图片被 LVGL 判为"零长度"而拒绘。
    dsc->data_size = (uint32_t)w * h * (alpha ? 3 : 2);
    dsc->data = data;
}

static bool load_action(act_t act) {
    if (s_act_loaded == act && s_act.data) return true;
    static const char *NAMES[] = { "run", "fight", "sleep", "victory" };
    app_asset_frames_t next;
    if (!app_assets_load_action(NAMES[act], &next)) {
        ESP_LOGE(TAG, "动作素材缺失: %s", NAMES[act]);
        return false;
    }
    app_asset_frames_free(&s_act);
    s_act = next;
    s_act_loaded = act;
    // 逐帧描述符数组随帧数分配。
    lv_free(s_act_dsc);
    s_act_dsc = lv_malloc(sizeof(lv_image_dsc_t) * s_act.frames);
    for (uint16_t i = 0; i < s_act.frames; i++) {
        fill_dsc(&s_act_dsc[i],
                 s_act.data + (size_t)i * s_act.w * s_act.h * SPRITE_BPP,
                 s_act.w, s_act.h, true);
    }
    return true;
}

static void set_action(act_t act) {
    if (act == s_act_current || !s_pet_img) return;
    bool loaded = load_action(act);
    s_act_current = act;   // 失败也记账:不重试,无帧时隐藏,避免每帧刷错误
    s_frame_idx = 0;
    s_frame_ms = 0;
    if (!loaded || !s_act.data) {
        lv_image_set_src(s_pet_img, NULL);
        return;
    }
    lv_image_set_src(s_pet_img, &s_act_dsc[0]);
    // 放大后以"显示底边贴地、水平中心不变"定位:
    // widget 仍是 64x64,2x 变换围绕其中心展开。
    lv_obj_set_pos(s_pet_img,
                   PET_X - s_act.w / 2,
                   GROUND_Y - s_act.h - s_act.h / 2);
}

static act_t decide_action(void) {
    if (s_victory_active) return ACT_VICTORY;
    app_runtime_t *rt = app_runtime();
    if (rt->focus.running) return ACT_FIGHT;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (tv.tv_sec >= APP_TIME_PLAUSIBLE_S &&
        app_time_in_rest(app_time_min_of_day(tv.tv_sec), rt->rest)) {
        return ACT_SLEEP;
    }
    return ACT_RUN;
}

// ---- 天气 ----
static void load_weather_sprites(app_weather_kind_t kind) {
    if (kind == APP_WEATHER_CLOUDY || kind == APP_WEATHER_SUNNY) {
        // 阴天无叠加;晴天的光斑用 snow 帧凑(P1 占位)。
    }
    if (s_wx_loaded) {
        app_asset_frames_free(&s_wx);
        s_wx_loaded = false;
    }
    static lv_image_dsc_t wx_dsc[4];
    if (kind == APP_WEATHER_RAIN || kind == APP_WEATHER_SNOW ||
        kind == APP_WEATHER_SUNNY) {
        if (!app_assets_load_weather(kind == APP_WEATHER_RAIN ? "rain" : "snow",
                                     &s_wx)) {
            for (int i = 0; i < WEATHER_SPRITES; i++) {
                lv_obj_add_flag(s_wx_img[i], LV_OBJ_FLAG_HIDDEN);
            }
            return;
        }
        s_wx_loaded = true;
        for (uint16_t i = 0; i < s_wx.frames && i < 4; i++) {
            fill_dsc(&wx_dsc[i], s_wx.data + (size_t)i * s_wx.w * s_wx.h * SPRITE_BPP,
                     s_wx.w, s_wx.h, true);
        }
        for (int i = 0; i < WEATHER_SPRITES; i++) {
            lv_image_set_src(s_wx_img[i], &wx_dsc[i % s_wx.frames]);
            lv_obj_clear_flag(s_wx_img[i], LV_OBJ_FLAG_HIDDEN);
            s_wx_x[i] = 20 + i * 80;
            s_wx_y[i] = (i * 37) % (MAP_H - s_wx.h);
        }
    } else {
        for (int i = 0; i < WEATHER_SPRITES; i++) {
            lv_obj_add_flag(s_wx_img[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void weather_tick(app_weather_kind_t kind, int dt_ms) {
    if (!s_wx_loaded || kind == APP_WEATHER_CLOUDY) return;
    for (int i = 0; i < WEATHER_SPRITES; i++) {
        if (kind == APP_WEATHER_RAIN) {
            s_wx_y[i] += 10 * dt_ms / TICK_MS;
        } else if (kind == APP_WEATHER_SNOW) {
            s_wx_y[i] += 2 * dt_ms / TICK_MS;
            s_wx_x[i] += ((i % 2) ? 1 : -1);
        } else {   // 晴天光斑:缓慢漂移
            s_wx_x[i] += (i % 2) ? 1 : -1;
        }
        if (s_wx_y[i] > MAP_H - s_wx.h) s_wx_y[i] = -s_wx.h + 4;
        if (s_wx_x[i] < -s_wx.w) s_wx_x[i] = MAP_W;
        if (s_wx_x[i] > MAP_W) s_wx_x[i] = -s_wx.w;
        lv_obj_set_pos(s_wx_img[i], s_wx_x[i], MAP_Y + s_wx_y[i]);
    }
}

// ---- 时段调色(运行时,不预烘 12 套地图) ----
static void apply_tint(app_tod_t tod) {
    switch (tod) {
    case APP_TOD_DAY:
        lv_obj_set_style_bg_opa(s_tint, LV_OPA_TRANSP, 0);
        break;
    case APP_TOD_DUSK:
        lv_obj_set_style_bg_color(s_tint, lv_color_hex(0xFF9A3C), 0);
        lv_obj_set_style_bg_opa(s_tint, LV_OPA_30, 0);
        break;
    default:
        lv_obj_set_style_bg_color(s_tint, lv_color_hex(0x101C3C), 0);
        lv_obj_set_style_bg_opa(s_tint, LV_OPA_60, 0);
        break;
    }
}

// ---- 主节拍 ----
static void tick(lv_timer_t *t) {
    (void)t;
    if (s_paused || !s_pet || !s_pet_img) return;
    app_runtime_t *rt = app_runtime();

    if (s_victory_active && now_ms() >= s_victory_until_ms) {
        s_victory_active = false;
    }
    set_action(decide_action());

    // 地图滚动(睡觉与胜利时静止)。
    if (s_act_current == ACT_RUN || s_act_current == ACT_FIGHT) {
        s_scroll_ms += SCROLL_PX_PER_S * TICK_MS;
        if (s_scroll_ms >= 1000) {
            s_map_offset += s_scroll_ms / 1000;
            s_scroll_ms %= 1000;
        }
        if (s_map_offset >= MAP_W) s_map_offset -= MAP_W;
        if (s_map_img[0]) lv_obj_set_pos(s_map_img[0], -s_map_offset, MAP_Y);
        if (s_map_img[1]) lv_obj_set_pos(s_map_img[1], MAP_W - s_map_offset, MAP_Y);
        // 装扮随地图同速左移,出屏后从右侧重入。
        if (s_deco_img) {
            s_deco_x -= SCROLL_PX_PER_S * TICK_MS / 1000;
            if (s_deco_x < -24) s_deco_x = MAP_W + 40;
            lv_obj_set_pos(s_deco_img, s_deco_x, GROUND_Y - s_deco.h);
        }
    }

    // 帧推进。
    if (s_act_current != ACT_NONE && s_act.data && s_act.frames > 1) {
        s_frame_ms += TICK_MS;
        uint32_t frame_ms = 1000 / (s_act.fps ? s_act.fps : 6);
        if (s_frame_ms >= frame_ms) {
            s_frame_ms -= frame_ms;
            s_frame_idx = (s_frame_idx + 1) % s_act.frames;
            lv_image_set_src(s_pet_img, &s_act_dsc[s_frame_idx]);
        }
    }

    static app_weather_kind_t s_last_kind = (app_weather_kind_t)-1;
    static app_tod_t s_last_tod = (app_tod_t)-1;
    app_weather_kind_t kind = app_weather_from_wmo(rt->weather_code);
    if (kind != s_last_kind) {
        load_weather_sprites(kind);
        s_last_kind = kind;
    }
    if (rt->tod != s_last_tod) {
        apply_tint(rt->tod);
        s_last_tod = rt->tod;
    }
    weather_tick(kind, TICK_MS);
}

void ui_pet_on_victory(void) {
    if (!s_pet) {
        ESP_LOGW(TAG, "胜利动画丢失: 宠物页未创建");
        return;
    }
    ESP_LOGI(TAG, "胜利动画启动 (页暂停=%d 当前动作=%d)",
             (int)s_paused, (int)s_act_current);
    s_victory_active = true;
    s_victory_until_ms = now_ms() + 2200;
    set_action(ACT_VICTORY);
}

void ui_pet_set_navigation(const app_home_t *home) {
    s_home = *home;
    ui_pet_refresh();
}

bool ui_pet_victory_active(void) {
    return s_victory_active && now_ms() < s_victory_until_ms;
}

void ui_pet_refresh(void) {
    if (!s_pet) return;
    app_runtime_t *rt = app_runtime();
    bool edit = s_home.adjusting;
    bool victory = ui_pet_victory_active();
    ui_theme_status_bar_refresh(&s_status);
    lv_label_set_text_fmt(s_lv_label, "Lv.%d", app_xp_level(rt->xp.total_xp));
    lv_bar_set_value(s_lv_bar, app_xp_level_permille(rt->xp.total_xp), LV_ANIM_OFF);
    lv_label_set_text_fmt(s_xp_values[0], "%u", rt->xp.today_count);
    lv_label_set_text_fmt(s_xp_values[1], "%u", rt->xp.total_xp);
    lv_label_set_text_fmt(s_xp_values[2], "%d", app_xp_to_next(rt->xp.total_xp));
    int64_t now = now_ms();
    if (now / 1000 >= APP_TIME_PLAUSIBLE_S) {
        int minute = app_time_min_of_day(now / 1000);
        lv_label_set_text_fmt(s_clock_label, "%02d:%02d", minute / 60, minute % 60);
    } else lv_label_set_text(s_clock_label, "--:--");

    ui_theme_select(s_head, !edit && !victory && s_home.target == APP_HOME_USAGE);
    if (s_home.target != APP_HOME_USAGE || edit || victory)
        lv_obj_set_style_bg_color(s_head, lv_color_hex(UI_BG), 0);
    ui_theme_select(s_focus_box, s_home.target == APP_HOME_FOCUS || edit);
    ui_theme_select(s_settings_box, !edit && s_home.target == APP_HOME_SETTINGS);
    lv_obj_set_style_text_font(s_head_value, edit || victory ? &app_font_24 : &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_head_value, lv_color_hex(edit ? UI_ACCENT : victory ? UI_GOLD : UI_INK), 0);
    lv_obj_set_pos(s_head_title, edit || victory ? 8 : 126, 4);
    lv_obj_set_width(s_head_title, edit || victory ? 208 : 90);
    lv_obj_set_pos(s_head_value, edit || victory ? 8 : 126, edit || victory ? 19 : 20);
    lv_obj_set_width(s_head_value, edit || victory ? 158 : 90);
    lv_obj_set_pos(s_head_note, edit || victory ? 8 : 126, edit || victory ? 44 : 43);
    lv_obj_set_width(s_head_note, edit || victory ? 208 : 90);
    lv_obj_set_style_text_color(s_head_note, lv_color_hex(edit ? UI_GOLD : UI_INK_DIM), 0);
    if (edit || victory) {
        lv_obj_add_flag(s_badge_name, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_badge_role, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_clear_flag(s_badge_name, LV_OBJ_FLAG_HIDDEN);
        bool synced_identity = rt->snap_valid && rt->snap.badge_name[0];
        const char *name = synced_identity ? rt->snap.badge_name : APP_BADGE_DEFAULT_NAME;
        const char *role = synced_identity ? rt->snap.badge_role : APP_BADGE_DEFAULT_ROLE;
        lv_point_t size;
        lv_text_get_size(&size, name, &app_font_24, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        bool compact = size.x > 112;
        lv_obj_set_style_text_font(s_badge_name, compact ? &app_font_12 : &app_font_24, 0);
        lv_obj_set_y(s_badge_name, role[0] ? (compact ? 14 : 9) : (compact ? 24 : 16));
        lv_label_set_text(s_badge_name, name);
        if (role[0]) {
            lv_obj_clear_flag(s_badge_role, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_badge_role, role);
        } else lv_obj_add_flag(s_badge_role, LV_OBJ_FLAG_HIDDEN);
    }
    char buf[24];
    if (victory) {
        lv_label_set_text(s_head_title, "专注完成");
        lv_label_set_text(s_head_value, "经验 +1");
        lv_label_set_text(s_head_note, "又向前一步");
    } else if (edit) {
        lv_label_set_text_fmt(s_head_title, "番茄计时 · %u 分钟", s_home.draft * 25);
        lv_label_set_text_fmt(s_head_value, s_home.draft ? "%u x 25 min" : "取消全部计时", s_home.draft);
        lv_label_set_text(s_head_note, s_home.draft ? "确认后将重新开始" : "已获经验保留");
    } else {
        lv_label_set_text(s_head_title, "今日 TOKEN");
        if (rt->snap_valid && rt->snap.has_daily_tokens) {
            app_fmt_daily_tokens(rt->snap.daily_tokens, buf, sizeof(buf));
            lv_label_set_text(s_head_value, buf);
            lv_label_set_text(s_head_note, "全账户消耗");
        } else {
            lv_label_set_text(s_head_value, "--");
            lv_label_set_text(s_head_note, "无数据");
        }
    }
    lv_label_set_text(s_focus_title, edit ? "番茄数量" : rt->focus.running ? "专注中" : "开始专注");
    if (edit) {
        lv_label_set_text_fmt(s_focus_value, "%u 个", s_home.draft);
        lv_label_set_text(s_focus_count, "上下");
    } else if (rt->focus.running) {
        app_fmt_clock(app_focus_remaining_ms(&rt->focus, now), buf, sizeof(buf));
        lv_label_set_text(s_focus_value, buf);
        lv_label_set_text_fmt(s_focus_count, "%u / %u", rt->focus.xp_granted, rt->focus.units);
    } else {
        lv_label_set_text(s_focus_value, "OK");
        lv_label_set_text(s_focus_count, "25 min");
    }
    for (int i = 0; i < APP_FOCUS_MAX_UNITS; i++) {
        bool filled = edit ? i < s_home.draft : (rt->focus.running || victory) && i < rt->focus.xp_granted;
        bool active = !edit && rt->focus.running && i == rt->focus.xp_granted;
        lv_obj_set_style_bg_opa(s_unit_dots[i], filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_unit_dots[i], active ? 2 : 1, 0);
        lv_obj_set_style_border_color(s_unit_dots[i], lv_color_hex(filled || active ? UI_ACCENT : UI_INK_DIM), 0);
    }
    if (!edit && rt->focus.running && rt->focus.xp_granted < APP_FOCUS_MAX_UNITS) {
        lv_obj_set_x(s_dot_center, 13 + rt->focus.xp_granted * 19);
        lv_obj_clear_flag(s_dot_center, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(s_dot_center, LV_OBJ_FLAG_HIDDEN);
    ui_theme_hint_set(s_hint, victory ? NULL : edit ? "数量" : "选择",
                      victory ? NULL : edit ? "确认" : s_home.target == APP_HOME_USAGE ? "用量" : s_home.target == APP_HOME_SETTINGS ? "设置" : "调整",
                      edit ? "放弃" : NULL);
}

void ui_pet_set_paused(bool paused) {
    s_paused = paused;
}

void ui_pet_destroy(void) {
    if (!s_pet) return;
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    lv_obj_delete(s_pet->screen);
    lv_free(s_pet);
    s_pet = NULL;
    s_pet_img = NULL;
    for (int i = 0; i < 2; i++) s_map_img[i] = NULL;
    for (int i = 0; i < WEATHER_SPRITES; i++) s_wx_img[i] = NULL;
    s_deco_img = NULL;
    s_clock_label = NULL;
    s_lv_label = NULL;
    s_lv_bar = NULL;
    for (int i = 0; i < APP_FOCUS_MAX_UNITS; i++) s_unit_dots[i] = NULL;
    s_tint = NULL;
    // 素材(s_map/s_act/s_act_dsc/s_wx/s_deco)保留:重建免重读。
}

ui_pet_t *ui_pet_create(void) {
    if (s_pet) return s_pet;
    lv_obj_t *scr = ui_theme_screen();
    s_pet = lv_malloc(sizeof(ui_pet_t));
    s_pet->screen = scr;
    ui_theme_status_bar_create(scr, &s_status);

    // 地图(两份 image 共享单缓冲)。
    if (app_assets_load_map(&s_map)) {
        fill_dsc(&s_map_dsc, s_map.data, s_map.w, s_map.h, false);
        for (int i = 0; i < 2; i++) {
            s_map_img[i] = lv_image_create(scr);
            lv_image_set_src(s_map_img[i], &s_map_dsc);
            lv_obj_set_pos(s_map_img[i], i == 0 ? 0 : MAP_W, MAP_Y);
        }
    } else {
        ESP_LOGE(TAG, "地图素材缺失,纯色降级");
        lv_obj_t *fallback = lv_obj_create(scr);
        lv_obj_set_pos(fallback, 0, MAP_Y);
        lv_obj_set_size(fallback, MAP_W, MAP_H);
        lv_obj_set_style_bg_color(fallback, lv_color_hex(0x1B3A2A), 0);
        lv_obj_set_style_radius(fallback, 0, 0);
        lv_obj_set_style_border_width(fallback, 0, 0);
    }

    // 时段调色层(盖在地图上、角色下,避免宠物被染色)。
    s_tint = lv_obj_create(scr);
    lv_obj_set_pos(s_tint, 0, MAP_Y);
    lv_obj_set_size(s_tint, MAP_W, MAP_H);
    lv_obj_set_style_bg_opa(s_tint, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_tint, 0, 0);
    lv_obj_set_style_border_width(s_tint, 0, 0);
    apply_tint(APP_TOD_DAY);

    // 天气精灵与装扮。
    for (int i = 0; i < WEATHER_SPRITES; i++) {
        s_wx_img[i] = lv_image_create(scr);
        lv_obj_add_flag(s_wx_img[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (app_assets_load_decoration(0, &s_deco)) {
        static lv_image_dsc_t deco_dsc;
        fill_dsc(&deco_dsc, s_deco.data, s_deco.w, s_deco.h, true);
        s_deco_img = lv_image_create(scr);
        lv_image_set_src(s_deco_img, &deco_dsc);
        s_deco_x = MAP_W + 60;
        lv_obj_set_pos(s_deco_img, s_deco_x, GROUND_Y - s_deco.h);
    }

    // 宠物本体(最上层)。
    s_pet_img = lv_image_create(scr);
    lv_image_set_scale(s_pet_img, PET_SCALE);
    lv_image_set_antialias(s_pet_img, false);   // 最近邻,像素风棱角分明
    s_act_loaded = ACT_NONE;
    s_act_current = ACT_NONE;
    set_action(ACT_RUN);

    // Overlay HUD is created after scenery; it never receives focus.
    lv_obj_t *hud = ui_theme_box(scr, 8, MAP_Y + 5, 224, 21, UI_BG);
    lv_obj_set_style_radius(hud, 3, 0);
    s_lv_label = ui_theme_label(hud, 6, 4, 48, &app_font_12, UI_GOLD, "Lv.1");
    s_lv_bar = lv_bar_create(hud);
    lv_obj_set_pos(s_lv_bar, 57, 9);
    lv_obj_set_size(s_lv_bar, 65, 4);
    lv_bar_set_range(s_lv_bar, 0, 1000);
    lv_obj_set_style_bg_color(s_lv_bar, lv_color_hex(UI_LINE), 0);
    lv_obj_set_style_bg_color(s_lv_bar, lv_color_hex(UI_GOLD), LV_PART_INDICATOR);
    s_clock_label = ui_theme_label(hud, 166, 4, 51, &app_font_12, UI_INK, "--:--");
    lv_obj_set_style_text_align(s_clock_label, LV_TEXT_ALIGN_RIGHT, 0);

    // 地图内的番茄记录:保持角色可见,文字固定在滚动场景之上。
    lv_obj_t *xp_overlay = ui_theme_box(scr, 150, MAP_Y + 42, 82, 81, UI_BG);
    lv_obj_set_style_bg_opa(xp_overlay, LV_OPA_70, 0);
    lv_obj_set_style_radius(xp_overlay, 4, 0);
    static const char *xp_names[] = { "今日", "累计", "还需" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *dot = ui_theme_box(xp_overlay, 5, 8 + i * 25, 5, 5, UI_WARN);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        ui_theme_label(xp_overlay, 13, 3 + i * 25, 35, &app_font_12, UI_INK, xp_names[i]);
        s_xp_values[i] = ui_theme_label(xp_overlay, 49, 3 + i * 25, 28, &app_font_12, UI_ACCENT, "0");
        lv_obj_set_style_text_align(s_xp_values[i], LV_TEXT_ALIGN_RIGHT, 0);
    }

    s_head = ui_theme_card(scr, 8, 23, 224, 59, false);
    s_badge_name = ui_theme_label(s_head, 8, 16, 112, &app_font_24, UI_INK, "你的名字");
    s_badge_role = ui_theme_label(s_head, 8, 42, 112, &app_font_12, UI_ACCENT, "");
    s_head_title = ui_theme_label(s_head, 126, 4, 90, &app_font_12, UI_INK_DIM, "");
    s_head_value = ui_theme_label(s_head, 126, 20, 90, &lv_font_montserrat_20, UI_INK, "--");
    s_head_note = ui_theme_label(s_head, 126, 43, 90, &app_font_12, UI_INK_DIM, "");

    s_focus_box = ui_theme_card(scr, 8, 252, 173, 44, true);
    s_focus_title = ui_theme_label(s_focus_box, 8, 5, 82, &app_font_12, UI_INK, "");
    s_focus_value = ui_theme_label(s_focus_box, 88, 5, 73, &app_font_12, UI_ACCENT, "");
    lv_obj_set_style_text_align(s_focus_value, LV_TEXT_ALIGN_RIGHT, 0);
    s_focus_count = ui_theme_label(s_focus_box, 110, 25, 51, &app_font_12, UI_INK_DIM, "");
    lv_obj_set_style_text_align(s_focus_count, LV_TEXT_ALIGN_RIGHT, 0);
    for (int i = 0; i < APP_FOCUS_MAX_UNITS; i++) {
        s_unit_dots[i] = ui_theme_box(s_focus_box, 9 + i * 19, 25, 11, 11, UI_ACCENT);
        lv_obj_set_style_radius(s_unit_dots[i], LV_RADIUS_CIRCLE, 0);
    }
    s_dot_center = ui_theme_box(s_focus_box, 13, 29, 3, 3, UI_ACCENT);
    lv_obj_set_style_radius(s_dot_center, LV_RADIUS_CIRCLE, 0);
    s_settings_box = ui_theme_card(scr, 187, 252, 45, 44, false);
    lv_obj_t *gear = ui_theme_label(s_settings_box, 0, 4, 41, &lv_font_montserrat_14, UI_INK_DIM, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_align(gear, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *caption = ui_theme_label(s_settings_box, 0, 25, 41, &app_font_12, UI_INK_DIM, "设置");
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    s_hint = ui_theme_hint_create(scr);
    app_home_reset(&s_home);
    ui_pet_refresh();
    s_timer = lv_timer_create(tick, TICK_MS, NULL);
    return s_pet;
}

lv_obj_t *ui_pet_screen(void) {
    return s_pet ? s_pet->screen : NULL;
}

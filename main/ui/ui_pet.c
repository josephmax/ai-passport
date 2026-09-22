// main/ui/ui_pet.c —— 宠物页实现。
//
// 内存策略(无 PSRAM 红线):地图条带 240×160×2=75K 单份堆缓冲,两个
// lv_image 共享同一数据源实现无缝循环;动作帧只驻留当前动作
// (切换时释放重载);天气/装扮小精灵按需加载。
#include "ui_pet.h"

#include "app_assets.h"
#include "app_focus.h"
#include "app_runtime.h"
#include "app_time.h"
#include "app_xp.h"
#include "esp_log.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "ui_pet";

#define MAP_W 240
#define MAP_H 160
#define MAP_Y 48
#define PET_X 80            // 屏宽 1/3 处
#define GROUND_Y (MAP_Y + MAP_H - 2)
#define SCROLL_PX_PER_S 30
#define WEATHER_SPRITES 3
#define TICK_MS 33

typedef enum { ACT_RUN = 0, ACT_FIGHT, ACT_SLEEP, ACT_VICTORY, ACT_NONE } act_t;

static ui_pet_t *s_pet;
static ui_status_bar_t s_status;
static lv_obj_t *s_hint;
static ui_page_dots_t s_dots;

// 地图:单份数据,两个 image 拼接循环。
static app_asset_frames_t s_map;
static lv_image_dsc_t s_map_dsc;
static lv_obj_t *s_map_img[2];
static int s_map_offset;

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
static lv_obj_t *s_unit_dots[APP_FOCUS_MAX_UNITS];
static lv_obj_t *s_info_bar;

static lv_timer_t *s_timer;
static bool s_paused;

static int64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void fill_dsc(lv_image_dsc_t *dsc, const uint8_t *data,
                     uint16_t w, uint16_t h) {
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.stride = w * 2;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565;
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
                 s_act.data + (size_t)i * s_act.w * s_act.h * 2,
                 s_act.w, s_act.h);
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
    lv_obj_set_pos(s_pet_img, PET_X, GROUND_Y - s_act.h);
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
            fill_dsc(&wx_dsc[i], s_wx.data + (size_t)i * s_wx.w * s_wx.h * 2,
                     s_wx.w, s_wx.h);
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
        s_map_offset += SCROLL_PX_PER_S * TICK_MS / 1000;
        if (s_map_offset >= MAP_W) s_map_offset -= MAP_W;
        lv_obj_set_pos(s_map_img[0], -s_map_offset, MAP_Y);
        lv_obj_set_pos(s_map_img[1], MAP_W - s_map_offset, MAP_Y);
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

    // 计时数字与单元点(挂在宠物页常驻)。
    if (rt->focus.running) {
        int64_t left = app_focus_remaining_ms(&rt->focus, now_ms());
        char buf[16];
        int total_sec = (int)((left + 500) / 1000);
        snprintf(buf, sizeof(buf), "%d:%02d", total_sec / 60, total_sec % 60);
        lv_label_set_text(s_clock_label, buf);
    } else {
        lv_label_set_text(s_clock_label, "");
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
    if (!s_pet) return;
    s_victory_active = true;
    s_victory_until_ms = now_ms() + 2200;
    set_action(ACT_VICTORY);
}

void ui_pet_refresh(void) {
    if (!s_pet || !s_info_bar) return;
    app_runtime_t *rt = app_runtime();
    ui_theme_status_bar_refresh(&s_status);

    int level = app_xp_level(rt->xp.total_xp);
    int to_next = app_xp_to_next(rt->xp.total_xp);
    lv_label_set_text_fmt(s_lv_label, "Lv.%d", level);
    lv_bar_set_value(s_lv_bar, app_xp_level_permille(rt->xp.total_xp), LV_ANIM_OFF);
    char info[48];
    snprintf(info, sizeof(info), "今 %u · 周 %u · 下一级 %d",
             rt->xp.today_count, rt->xp.week_count, to_next);
    lv_label_set_text(s_info_bar, info);
    for (int i = 0; i < APP_FOCUS_MAX_UNITS; i++) {
        lv_obj_set_style_bg_color(s_unit_dots[i], lv_color_hex(
            i < rt->focus.units ? UI_ACCENT : UI_LINE), 0);
    }
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
    s_info_bar = NULL;
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

    // HUD:等级徽标 + 级内进度条(80px)。
    s_lv_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lv_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lv_label, lv_color_hex(UI_GOLD), 0);
    lv_obj_set_pos(s_lv_label, 10, 30);
    lv_label_set_text(s_lv_label, "Lv.1");

    s_lv_bar = lv_bar_create(scr);
    lv_obj_set_size(s_lv_bar, 80, 8);
    lv_obj_set_pos(s_lv_bar, 64, 34);
    lv_bar_set_range(s_lv_bar, 0, 1000);
    lv_obj_set_style_bg_color(s_lv_bar, lv_color_hex(UI_LINE), 0);
    lv_obj_set_style_bg_color(s_lv_bar, lv_color_hex(UI_GOLD), LV_PART_INDICATOR);

    // 计时时钟 + 单元点。
    s_clock_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_clock_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_clock_label, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_pos(s_clock_label, 24, 218);
    for (int i = 0; i < APP_FOCUS_MAX_UNITS; i++) {
        s_unit_dots[i] = lv_obj_create(scr);
        lv_obj_set_size(s_unit_dots[i], 10, 10);
        lv_obj_set_pos(s_unit_dots[i], 144 + i * 18, 230);
        lv_obj_set_style_radius(s_unit_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_unit_dots[i], lv_color_hex(UI_LINE), 0);
        lv_obj_set_style_bg_opa(s_unit_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_unit_dots[i], 0, 0);
    }

    // 地图(两份 image 共享单缓冲)。
    if (app_assets_load_map(&s_map)) {
        fill_dsc(&s_map_dsc, s_map.data, s_map.w, s_map.h);
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
        fill_dsc(&deco_dsc, s_deco.data, s_deco.w, s_deco.h);
        s_deco_img = lv_image_create(scr);
        lv_image_set_src(s_deco_img, &deco_dsc);
        s_deco_x = MAP_W + 60;
        lv_obj_set_pos(s_deco_img, s_deco_x, GROUND_Y - s_deco.h);
    }

    // 宠物本体(最上层)。
    s_pet_img = lv_image_create(scr);
    s_act_loaded = ACT_NONE;
    s_act_current = ACT_NONE;
    set_action(ACT_RUN);

    // 底部信息条(让出统一提示栏的高度)与按键提示栏。
    s_info_bar = lv_label_create(scr);
    lv_obj_set_style_text_font(s_info_bar, &app_font_12, 0);
    lv_obj_set_style_text_color(s_info_bar, lv_color_hex(UI_INK_DIM), 0);
    lv_obj_align(s_info_bar, LV_ALIGN_BOTTOM_MID, 0, -26);
    s_hint = ui_theme_hint_create(scr);
    ui_theme_hint_set(s_hint, "切页", "番茄", "取消");
    ui_theme_page_dots_create(scr, &s_dots);

    ui_pet_refresh();
    s_timer = lv_timer_create(tick, TICK_MS, NULL);
    return s_pet;
}

lv_obj_t *ui_pet_screen(void) {
    return s_pet ? s_pet->screen : NULL;
}

ui_page_dots_t *ui_pet_dots(void) {
    return &s_dots;
}

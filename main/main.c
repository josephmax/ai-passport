// main/main.c —— 多合一挂坠应用入口(构建中):显示 + 按键最小骨架。
// 最终形态由 ui_shell 取代:三页导航/宠物/仪表盘/设置(规格 §3)。
#include "bsp_display.h"

#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "pendant";

void app_main(void) {
    ESP_LOGI(TAG, "多合一挂坠启动");
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示初始化失败");
        return;
    }
    bsp_display_backlight(60);
    if (bsp_lvgl_lock(1000)) {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x101828), 0);
        lv_obj_t *label = lv_label_create(scr);
        lv_label_set_text(label, "Multi-Pendant");
        lv_obj_center(label);
        bsp_lvgl_unlock();
    }
}

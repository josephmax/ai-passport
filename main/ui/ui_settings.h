#pragma once
#include "lvgl.h"
#include "app_store.h"

typedef struct { lv_obj_t *screen; } ui_settings_t;
typedef struct {
    bool save, cancel_provisioning;
    app_settings_t settings;
} ui_settings_work_t;
ui_settings_t *ui_settings_create(void);
// Rolls back an unconfirmed adjustment, including forced victory/wake exits.
void ui_settings_destroy(void);
lv_obj_t *ui_settings_screen(void);
// false only for long OK on the list: Shell returns home.
bool ui_settings_key(bool ok_short, bool ok_long, bool up, bool down);
void ui_settings_refresh(void);
// Capture under LVGL lock; perform storage/network work outside that lock.
ui_settings_work_t ui_settings_take_work(void);

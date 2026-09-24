#pragma once

#include "app_focus.h"

typedef enum { APP_HOME_USAGE, APP_HOME_FOCUS, APP_HOME_SETTINGS } app_home_target_t;
typedef enum { APP_HOME_UP, APP_HOME_DOWN, APP_HOME_OK, APP_HOME_BACK } app_home_key_t;
typedef enum {
    APP_HOME_NONE, APP_HOME_OPEN_USAGE, APP_HOME_OPEN_SETTINGS,
    APP_HOME_APPLY_FOCUS, APP_HOME_LIMIT,
} app_home_action_t;

typedef struct {
    app_home_target_t target;
    bool adjusting;
    uint8_t draft;
} app_home_t;

// View-only draft: editing and backing out never mutate the running timer.
void app_home_reset(app_home_t *home);
app_home_action_t app_home_key(app_home_t *home, app_home_key_t key,
                               const app_focus_state_t *focus, uint8_t preset);

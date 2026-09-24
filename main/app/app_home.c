#include "app_home.h"

void app_home_reset(app_home_t *home) {
    *home = (app_home_t){ .target = APP_HOME_FOCUS, .draft = 1 };
}

app_home_action_t app_home_key(app_home_t *home, app_home_key_t key,
                               const app_focus_state_t *focus, uint8_t preset) {
    if (home->adjusting) {
        if (key == APP_HOME_BACK) home->adjusting = false;
        else if (key == APP_HOME_OK) {
            home->adjusting = false;
            return APP_HOME_APPLY_FOCUS;
        } else if (key == APP_HOME_UP) {
            if (home->draft == APP_FOCUS_MAX_UNITS) return APP_HOME_LIMIT;
            home->draft++;
        } else if (key == APP_HOME_DOWN && home->draft > 0) home->draft--;
    } else if (key == APP_HOME_UP || key == APP_HOME_DOWN) {
        home->target = (app_home_target_t)((home->target + (key == APP_HOME_UP ? 2 : 1)) % 3);
    } else if (key == APP_HOME_OK) {
        if (home->target == APP_HOME_USAGE) return APP_HOME_OPEN_USAGE;
        if (home->target == APP_HOME_SETTINGS) return APP_HOME_OPEN_SETTINGS;
        home->adjusting = true;
        home->draft = focus->running ? focus->units : preset;
        if (home->draft > APP_FOCUS_MAX_UNITS) home->draft = 1;
    }
    return APP_HOME_NONE;
}

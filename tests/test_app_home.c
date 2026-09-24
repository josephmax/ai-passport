#include "app/app_home.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    app_home_t h;
    app_focus_state_t f = {0};
    app_home_reset(&h);
    assert(h.target == APP_HOME_FOCUS && !h.adjusting);
    assert(app_home_key(&h, APP_HOME_BACK, &f, 1) == APP_HOME_NONE);
    app_home_key(&h, APP_HOME_UP, &f, 1);
    assert(app_home_key(&h, APP_HOME_OK, &f, 1) == APP_HOME_OPEN_USAGE);
    app_home_key(&h, APP_HOME_UP, &f, 1);
    assert(app_home_key(&h, APP_HOME_OK, &f, 1) == APP_HOME_OPEN_SETTINGS);
    app_home_key(&h, APP_HOME_DOWN, &f, 1);
    assert(h.target == APP_HOME_USAGE);
    app_home_key(&h, APP_HOME_DOWN, &f, 1);
    app_focus_configure(&f, 3, 1000);
    app_focus_state_t before = f;
    app_home_key(&h, APP_HOME_OK, &f, 2);
    assert(h.adjusting && h.draft == 3);
    app_home_key(&h, APP_HOME_UP, &f, 2);
    app_home_key(&h, APP_HOME_UP, &f, 2);
    assert(app_home_key(&h, APP_HOME_UP, &f, 2) == APP_HOME_LIMIT && h.draft == 5);
    app_home_key(&h, APP_HOME_BACK, &f, 2);
    assert(!h.adjusting && memcmp(&f, &before, sizeof(f)) == 0);
    app_home_key(&h, APP_HOME_OK, &f, 2);
    for (int i = 0; i < 8; i++) app_home_key(&h, APP_HOME_DOWN, &f, 2);
    assert(h.draft == 0 && f.running);
    assert(app_home_key(&h, APP_HOME_OK, &f, 2) == APP_HOME_APPLY_FOCUS);
    assert(!h.adjusting && memcmp(&f, &before, sizeof(f)) == 0);
    app_focus_configure(&f, h.draft, 5000);
    assert(!f.running);
    app_home_key(&h, APP_HOME_OK, &f, 0);
    assert(h.draft == 0); // last confirmed zero is a real preset
    app_home_reset(&h); // wake/victory discard a draft and reset cursor
    assert(!h.adjusting && h.target == APP_HOME_FOCUS);
    puts("test_app_home: navigation, drafts, limits, cancel and wake PASS");
}

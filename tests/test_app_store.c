// NVS length semantics are significant: a zero-capacity read cannot restore a blob.
#include "app/app_store.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { char key[16]; unsigned char data[512]; size_t size; } record_t;
static record_t records[12];
static unsigned count;
static record_t *find(const char *key) {
    for (unsigned i = 0; i < count; i++) if (!strcmp(records[i].key, key)) return &records[i];
    return NULL;
}
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *handle) {
    (void)ns; (void)mode; *handle = 1; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *size) {
    (void)h;
    record_t *r = find(key);
    if (!r) return ESP_FAIL;
    if (!out) { *size = r->size; return ESP_OK; }
    if (*size < r->size) { *size = r->size; return ESP_FAIL; }
    memcpy(out, r->data, r->size); *size = r->size; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t size) {
    (void)h;
    record_t *r = find(key);
    if (!r) { assert(count < 12); r = &records[count++]; snprintf(r->key, sizeof(r->key), "%s", key); }
    assert(size <= sizeof(r->data));
    memcpy(r->data, data, size); r->size = size; return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *out, size_t *n) { return nvs_get_blob(h, k, out, n); }
esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *s) { return nvs_set_blob(h, k, s, strlen(s) + 1); }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { count = 0; return ESP_OK; }
#define SCALAR(type, name) \
    esp_err_t nvs_get_##name(nvs_handle_t h, const char *k, type *v) { size_t n = sizeof(*v); return nvs_get_blob(h, k, v, &n); } \
    esp_err_t nvs_set_##name(nvs_handle_t h, const char *k, type v) { return nvs_set_blob(h, k, &v, sizeof(v)); }
SCALAR(uint8_t, u8)
SCALAR(int32_t, i32)
SCALAR(int64_t, i64)

int main(void) {
    app_store_init();
    assert(app_store_focus_preset() == 1);
    app_settings_t setting = app_store_settings();
    setting.brightness = 5; setting.auto_off_s = 120;
    app_store_save_settings(&setting);
    assert(app_store_settings().brightness == 5 && app_store_settings().auto_off_s == 120);
    app_focus_state_t f = {0};
    app_focus_configure(&f, 4, 1000);
    f.xp_granted = 2;
    app_store_save_focus(&f);
    app_focus_state_t restored = app_store_focus();
    assert(restored.running && restored.units == 4 && restored.xp_granted == 2 && restored.end_at_ms == f.end_at_ms);
    app_xp_store_t xp = {0}; xp.total_xp = 123;
    app_store_save_xp(&xp); assert(app_store_xp().total_xp == 123);
    app_store_save_focus_preset(0); assert(app_store_focus_preset() == 0);
    app_store_save_focus_preset(5); assert(app_store_focus_preset() == 5);
    app_store_save_focus_preset(6); assert(app_store_focus_preset() == 5);
    // Wrong-sized legacy/corrupt data must not partially overwrite defaults.
    unsigned char bad = 255;
    nvs_set_blob(1, "set", &bad, 1);
    assert(app_store_settings().brightness == 3);
    nvs_set_blob(1, "focus", &bad, 1);
    assert(!app_store_focus().running);
    puts("test_app_store: settings, focus, XP, preset restoration PASS");
}

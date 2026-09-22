# 多合一挂坠应用(见 docs/specs/2026-09-22-multi-pendant-app.md)。
# 基线 demo 源文件(demo_*.c/ui_pixel*.c)保留在仓库中供主机故障注入测试
# 与后续参考,但不再进入固件构建 —— 衍生应用必须使用自己的 UI(ai-guide)。
idf_component_register(
    SRCS "main.c"
         "app/app_focus.c"
         "app/app_xp.c"
         "app/app_time.c"
         "app/app_weather.c"
         "app/app_snapshot.c"
         "app/app_fmt.c"
         "app/app_store.c"
         "app/app_assets.c"
         "app/app_runtime.c"
         "app/app_service.c"
         "app/app_power.c"
         "app/app_audio_fx.c"
         "ui/ui_theme.c"
         "ui/ui_dash.c"
         "ui/ui_pet.c"
         "ui/ui_settings.c"
         "ui/ui_shell.c"
         "fonts/app_font_12.c"
         "fonts/app_font_24.c"
    INCLUDE_DIRS "." "app" "ui"
    REQUIRES bsp nvs_flash esp_timer esp_event esp_netif esp_wifi
    PRIV_REQUIRES json joltwallet__littlefs esp_http_server esp_http_client
    EMBED_FILES "assets_default/pendant_default_bundle.bin"
                "assets_default/pendant_victory.pcm"
                "assets_default/pendant_beep.pcm"
)

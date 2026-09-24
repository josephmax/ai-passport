# 多合一挂坠应用(见 docs/specs/2026-09-22-multi-pendant-app.md)。
# 基线 demo 源文件(demo_*.c/ui_pixel*.c)保留在仓库中供主机故障注入测试
# 与后续参考,但不再进入固件构建 —— 衍生应用必须使用自己的 UI(ai-guide)。
idf_component_register(
    SRCS "main.c"
         "app/app_focus.c"
         "app/app_home.c"
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
         "app/app_net.c"
         "app/app_sync.c"
         "app/app_prov.c"
         "app/app_debug.c"
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

# 调试开关:构建时设 PENDANT_FAST_FOCUS=1 把番茄单元从 25 分钟缩到 10 秒,
# 真机快速验证完成链路(经验/胜利动画/音效)。不设置永远是产品语义。
if(DEFINED ENV{PENDANT_FAST_FOCUS})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PENDANT_FAST_FOCUS=1)
endif()

# 调试对时基准:构建机以"本地墙钟编码"历元注入(真 UTC + 时区偏移),未配网
# 设备开机偏差超 1 小时即重置。仅调试构建使用,不设置则不参与编译。
if(DEFINED ENV{PENDANT_BOOT_EPOCH})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        PENDANT_BOOT_EPOCH=$ENV{PENDANT_BOOT_EPOCH})
endif()

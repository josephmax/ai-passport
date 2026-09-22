// main/ui/ui_shell.h —— 应用外壳:三页导航 + 键位路由 + 电源/事件联动。
#pragma once

#include <stdbool.h>

// 初始化三页并启动按键分发;须在 LVGL 就绪后调用。
void ui_shell_init(void);

// 供 main 在启动各服务后触发一次全量刷新。
void ui_shell_boot_refresh(void);

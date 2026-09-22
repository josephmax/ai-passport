// main/app/app_net.h —— Wi-Fi STA 管理(凭证来自 NVS,配网写入见 app_prov)。
#pragma once

#include <stdbool.h>

// 初始化 netif/事件循环/Wi-Fi STA 并用已存凭证连接。无凭证时保持离线。
// 幂等;返回 false 仅当日志级初始化失败(应用仍可离线运行)。
bool app_net_init(void);

// 当前是否拿到 IP。
bool app_net_connected(void);

// main/app/app_prov.h —— SoftAP 配网(captive portal):热点 + DNS + 表单页。
//
// 流程(配置中心规格 §3):设置页进入 → 设备发热点 Passport-XXXX → 手机
// 连接后弹出配网页(填 Wi-Fi/服务地址/配对码)→ 设备存凭证、连 Wi-Fi、
// 配对换令牌 → 完成进入每小时同步。任一步失败回到热点模式等待重试。
#pragma once

#include <stdbool.h>

typedef enum {
    APP_PROV_IDLE = 0,
    APP_PROV_AP_UP,        // 热点已开,等待手机提交表单
    APP_PROV_CONNECTING,   // 已收表单,正在连 Wi-Fi
    APP_PROV_PAIRING,      // 正在向服务换取令牌
    APP_PROV_DONE,         // 成功
    APP_PROV_FAILED,       // 失败,回到 AP_UP
} app_prov_state_t;

app_prov_state_t app_prov_state(void);

// 启动配网(切 AP 模式)。幂等;已在进行中直接返回 true。
bool app_prov_start(void);

// 取消配网(熄屏/用户退出时调用):停 DNS/HTTP/AP,回 STA 模式。
void app_prov_cancel(void);

// 当前热点名(如 "Passport-A1B2"),未启动为 NULL。
const char *app_prov_ap_name(void);

// main/app/app_sync.h —— 每小时同步:快照拉取 + generatedAt 对时 + 素材包更新。
// 互斥红线:同步进行中暂停音频与帧素材加载(规格 §11)。
#pragma once

#include <stdbool.h>

// 启动同步任务(注册 IP 事件自动触发;未配对则空转)。
bool app_sync_init(void);

// 手动触发(设置页"立即同步")。
void app_sync_request_now(void);

// 用一次性配对码向服务换取设备令牌(配网流程连接 Wi-Fi 成功后调用)。
bool app_sync_pair(const char *base_url, const char *code);

bool app_sync_busy(void);

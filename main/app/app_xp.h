// main/app/app_xp.h —— 经验与等级(纯逻辑)。规则见规格 §6.2:
// 单位=番茄;分段定值升级表 1–9级5个/级、10–29级15、30–59级25、60–98级50、
// 99级100(终局),满级100=累计3145番茄。今日/本周计数分别于本地 0 点、
// 周一 0 点清零;经验与等级永不清零。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define APP_XP_MAX_LEVEL 100
#define APP_XP_TOTAL_MAX 3145   // 满级累计番茄数(与分段表强一致,单测校验)

// 等级=分段表反查,纯整数运算。total 越界按 [0, 3145] 收敛。
int app_xp_level(int total_xp);

// 升到下一级还差几个番茄;满级返回 0。
int app_xp_to_next(int total_xp);

// 达到 level 所需累计番茄数(等级 1 起点 0)。level 越界收敛。
int app_xp_level_base(int level);

// 当前级内进度,0..1000 千分比;满级返回 1000。
int app_xp_level_permille(int total_xp);

// ---- 今日/本周计数 + 持久化载荷(NVS 直接存这个结构) ----
typedef struct {
    uint16_t total_xp;     // 累计番茄(经验),上限 APP_XP_TOTAL_MAX
    uint16_t today_count;  // 今日番茄,本地 0 点清零
    uint16_t week_count;   // 本周番茄,周一 0 点清零
    uint32_t last_day;     // 最后一次活动的本地日索引(now/86400)
    uint32_t last_week;    // 最后一次活动的周索引(周一锚点,见 app_time)
} app_xp_store_t;

void app_xp_store_reset(app_xp_store_t *st);

// 跨日/跨周回零。day_idx/week_idx 由 app_time 提供;返回是否有变化(需写 NVS)。
bool app_xp_rollover(app_xp_store_t *st, uint32_t day_idx, uint32_t week_idx);

// 完成一个番茄计时单元:today/week/total 同步 +1,total 封顶。
void app_xp_add_unit(app_xp_store_t *st);

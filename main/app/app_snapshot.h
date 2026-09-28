// main/app/app_snapshot.h —— 用量快照解析(规格 §4.3)。
//
// JSON → 定长结构体,解析一次后 UI 只碰结构体。百分比等展示口径由
// app_snapshot_permille 从 used/cap 整数算出(cap 缺省时返回 has_cap=false,
// 调用方显示纯数值)。离线标注基于 generatedAt 与当前时刻的差值。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_SNAPSHOT_MAX_ACCOUNTS 6
#define APP_SNAPSHOT_MAX_AGENTS 8
#define APP_SNAPSHOT_LABEL_MAX 24
#define APP_BADGE_NAME_MAX 32
#define APP_BADGE_ROLE_MAX 24

typedef struct {
    double used;
    double cap;            // 仅当 has_cap 时有效
    char unit[8];          // "h" / "" / ...
    char reset_at[24];     // ISO 或短文本,原样展示
    bool has_cap;
} app_quota_t;

typedef struct {
    char provider[16];     // local|codex|glm|deepseek (legacy claude|chatgpt accepted)
    char label[APP_SNAPSHOT_LABEL_MAX];
    app_quota_t weekly;
    app_quota_t rolling5h;
    app_quota_t weekly_tokens;   // cap null = 无预算,显示纯数值
    bool has_weekly;
    bool has_rolling5h;
    bool has_weekly_tokens;
} app_account_t;

typedef struct {
    char agent[16];
    bool has_daily_tokens;
    bool has_weekly_tokens;
    double daily_tokens;
    double weekly_tokens;
    bool has_rolling5h;
    bool has_weekly;
    app_quota_t rolling5h;
    app_quota_t weekly;
} app_agent_t;

typedef struct {
    int schema;
    bool has_daily_tokens;     // absent/null is unknown, never zero
    double daily_tokens;       // local Agent logs since host-local midnight
    char badge_name[APP_BADGE_NAME_MAX]; // UTF-8 display name; empty uses device placeholder
    char badge_role[APP_BADGE_ROLE_MAX]; // optional role shown below the name
    int64_t generated_at_ms;   // 已折算为本地历元毫秒
    int64_t served_at_ms;      // 本次响应时间;与缓存生成时间分开用于校时
    int account_count;         // 主力账户(服务端保证第一位)在前
    app_account_t accounts[APP_SNAPSHOT_MAX_ACCOUNTS];
    int agent_count;
    app_agent_t agents[APP_SNAPSHOT_MAX_AGENTS];
    int weather_code;          // WMO
    char city[24];
    int sunrise_min;           // "06:12" -> 372
    int sunset_min;
    int bundle_version;        // assetBundle.version
} app_snapshot_t;

// 解析失败返回 false;成功时 out 填充,坏字段按缺省处理(离线友好)。
bool app_snapshot_parse(const char *json, size_t len, app_snapshot_t *out);

// used/cap -> 千分比 0..1000;无 cap 返回 false。
bool app_snapshot_permille(const app_quota_t *q, int *out_permille);

// 离线时长(小时,向上取整)。不足 1 小时返回 0。
int app_snapshot_offline_hours(int64_t generated_at_ms, int64_t now_ms);

// "06:12" -> 372;失败返回 -1。
int app_snapshot_parse_hhmm(const char *s);

// ISO8601 "2026-09-22T06:00:00+08:00" -> 本地历元秒。
// 设备时钟走本地时间,直接取墙钟数字;偏移后缀仅校验格式。失败返回 false。
bool app_snapshot_parse_iso8601(const char *s, int64_t *out_epoch_s);

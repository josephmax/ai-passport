// main/app/app_snapshot.c —— 快照解析实现。依赖 cJSON(IDF 自带;主机单测用
// tests/third_party/cJSON 的同源拷贝)。解析宽容:单字段坏不整体失败。
#include "app_snapshot.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void copy_str(char *dst, size_t cap, const cJSON *node) {
    if (!cJSON_IsString(node) || node->valuestring == NULL) return;
    strncpy(dst, node->valuestring, cap - 1);
    dst[cap - 1] = '\0';
}

static bool parse_quota(const cJSON *node, app_quota_t *q) {
    memset(q, 0, sizeof(*q));
    if (!cJSON_IsObject(node)) return false;
    const cJSON *used = cJSON_GetObjectItemCaseSensitive(node, "used");
    const cJSON *cap = cJSON_GetObjectItemCaseSensitive(node, "cap");
    const cJSON *unit = cJSON_GetObjectItemCaseSensitive(node, "unit");
    const cJSON *reset = cJSON_GetObjectItemCaseSensitive(node, "resetAt");
    if (!cJSON_IsNumber(used) || !isfinite(used->valuedouble) || used->valuedouble < 0) return false;
    q->used = used->valuedouble;
    if (cJSON_IsNumber(cap)) {
        q->cap = cap->valuedouble;
        q->has_cap = true;
    }
    copy_str(q->unit, sizeof(q->unit), unit);
    copy_str(q->reset_at, sizeof(q->reset_at), reset);
    return true;
}

static bool parse_account(const cJSON *node, app_account_t *acc) {
    memset(acc, 0, sizeof(*acc));
    if (!cJSON_IsObject(node)) return false;
    copy_str(acc->provider, sizeof(acc->provider),
             cJSON_GetObjectItemCaseSensitive(node, "provider"));
    copy_str(acc->label, sizeof(acc->label),
             cJSON_GetObjectItemCaseSensitive(node, "label"));
    const cJSON *quotas = cJSON_GetObjectItemCaseSensitive(node, "quotas");
    if (cJSON_IsObject(quotas)) {
        acc->has_weekly = parse_quota(
            cJSON_GetObjectItemCaseSensitive(quotas, "weekly"), &acc->weekly);
        acc->has_rolling5h = parse_quota(
            cJSON_GetObjectItemCaseSensitive(quotas, "rolling5h"), &acc->rolling5h);
        acc->has_weekly_tokens = parse_quota(
            cJSON_GetObjectItemCaseSensitive(quotas, "weeklyTokens"),
            &acc->weekly_tokens);
    }
    return acc->provider[0] != '\0';
}

bool app_snapshot_parse(const char *json, size_t len, app_snapshot_t *out) {
    memset(out, 0, sizeof(*out));
    if (json == NULL || len == 0) return false;
    char *buf = malloc(len + 1);
    if (buf == NULL) return false;
    memcpy(buf, json, len);
    buf[len] = '\0';
    cJSON *root = cJSON_ParseWithLength(buf, len);
    free(buf);
    if (root == NULL) return false;

    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    if (cJSON_IsNumber(schema)) out->schema = schema->valueint;
    copy_str(out->badge_name, sizeof(out->badge_name),
             cJSON_GetObjectItemCaseSensitive(root, "badgeName"));
    copy_str(out->badge_role, sizeof(out->badge_role),
             cJSON_GetObjectItemCaseSensitive(root, "badgeRole"));
    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "dailyTokens");
    const cJSON *used = cJSON_GetObjectItemCaseSensitive(daily, "used");
    if (cJSON_IsNumber(used) && isfinite(used->valuedouble) && used->valuedouble >= 0) {
        out->has_daily_tokens = true;
        out->daily_tokens = used->valuedouble;
    }

    const cJSON *generated = cJSON_GetObjectItemCaseSensitive(root, "generatedAt");
    int64_t gen_s = 0;
    if (cJSON_IsString(generated) &&
        app_snapshot_parse_iso8601(generated->valuestring, &gen_s)) {
        out->generated_at_ms = gen_s * 1000;
    }

    const cJSON *accounts = cJSON_GetObjectItemCaseSensitive(root, "accounts");
    if (cJSON_IsArray(accounts)) {
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, accounts) {
            if (out->account_count >= APP_SNAPSHOT_MAX_ACCOUNTS) break;
            app_account_t acc;
            if (parse_account(it, &acc)) {
                out->accounts[out->account_count++] = acc;
            }
        }
    }

    const cJSON *weather = cJSON_GetObjectItemCaseSensitive(root, "weather");
    if (cJSON_IsObject(weather)) {
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(weather, "code");
        if (cJSON_IsNumber(code)) out->weather_code = code->valueint;
        copy_str(out->city, sizeof(out->city),
                 cJSON_GetObjectItemCaseSensitive(weather, "city"));
        const cJSON *sr = cJSON_GetObjectItemCaseSensitive(weather, "sunrise");
        const cJSON *ss = cJSON_GetObjectItemCaseSensitive(weather, "sunset");
        if (cJSON_IsString(sr) && sr->valuestring)
            out->sunrise_min = app_snapshot_parse_hhmm(sr->valuestring);
        if (cJSON_IsString(ss) && ss->valuestring)
            out->sunset_min = app_snapshot_parse_hhmm(ss->valuestring);
    }

    const cJSON *bundle = cJSON_GetObjectItemCaseSensitive(root, "assetBundle");
    if (cJSON_IsObject(bundle)) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(bundle, "version");
        if (cJSON_IsNumber(v)) out->bundle_version = v->valueint;
    }

    cJSON_Delete(root);
    return out->schema > 0 && out->account_count > 0;
}

bool app_snapshot_permille(const app_quota_t *q, int *out_permille) {
    if (!q->has_cap || q->cap <= 0.0) return false;
    double p = q->used / q->cap * 1000.0;
    if (p < 0) p = 0;
    if (p > 1000) p = 1000;
    *out_permille = (int)(p + 0.5);
    return true;
}

int app_snapshot_offline_hours(int64_t generated_at_ms, int64_t now_ms) {
    int64_t delta_ms = now_ms - generated_at_ms;
    if (delta_ms <= 0) return 0;
    return (int)((delta_ms + 3599999) / 3600000);   // 向上取整
}

int app_snapshot_parse_hhmm(const char *s) {
    int hh = 0, mm = 0;
    if (s == NULL || sscanf(s, "%d:%d", &hh, &mm) != 2) return -1;
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return -1;
    return hh * 60 + mm;
}

bool app_snapshot_parse_iso8601(const char *s, int64_t *out_epoch_s) {
    // 严格子集:YYYY-MM-DDTHH:MM[:SS](Z|±HH:MM)。
    // 设备系统时钟走本地时间(见 app_time.h),故直接取【墙钟数字】当本地历元,
    // 偏移后缀只校验格式不参与换算 —— 服务端按用户时区(+08:00)生成墙钟。
    int y, mo, d, h, mi, sec = 0;
    int n = sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec);
    if (n < 5) return false;
    if (n < 6) sec = 0;
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    if (h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 59) return false;

    // days from civil(Howard Hinnant 算法),避免依赖 tm 结构的时区行为。
    int64_t yy = y;
    if (mo <= 2) { yy -= 1; mo += 12; }
    int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
    int64_t yoe = yy - era * 400;
    int64_t doy = (153 * (mo - 3) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + doe - 719468;
    *out_epoch_s = days * 86400 + h * 3600 + mi * 60 + sec;
    return true;
}

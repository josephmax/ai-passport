// tests/test_app_snapshot.c —— 快照解析 + 格式化主机单测(规格 §4.3/§12)。
// 使用 tests/third_party/cJSON(与 IDF 5.5.3 自带 cJSON 同源的拷贝)。
#include "app/app_fmt.h"
#include "app/app_snapshot.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *SAMPLE =
    "{"
    "\"schema\":1,"
    "\"generatedAt\":\"2026-09-22T06:00:00+08:00\","
    "\"accounts\":["
    " {\"provider\":\"claude\",\"label\":\"工作号\",\"quotas\":{"
    "   \"weekly\":{\"used\":62,\"cap\":140,\"unit\":\"h\",\"resetAt\":\"周三\"},"
    "   \"rolling5h\":{\"used\":18,\"cap\":36,\"unit\":\"h\",\"resetAt\":\"...\"},"
    "   \"weeklyTokens\":{\"used\":412000,\"cap\":null}}},"
    " {\"provider\":\"glm\",\"label\":\"个人\",\"quotas\":{"
    "   \"weeklyTokens\":{\"used\":9000,\"cap\":500000}}}"
    "],"
    "\"weather\":{\"code\":61,\"sunrise\":\"06:12\",\"sunset\":\"18:05\",\"city\":\"上海\"},"
    "\"assetBundle\":{\"version\":3}"
    "}";

static void test_parse_sample(void) {
    app_snapshot_t snap;
    assert(app_snapshot_parse(SAMPLE, strlen(SAMPLE), &snap));
    assert(snap.schema == 1);
    assert(snap.account_count == 2);
    // 主力账户第一位
    assert(strcmp(snap.accounts[0].provider, "claude") == 0);
    assert(strcmp(snap.accounts[0].label, "工作号") == 0);
    assert(snap.accounts[0].has_weekly && snap.accounts[0].has_rolling5h);
    assert(snap.accounts[0].weekly.used == 62 && snap.accounts[0].weekly.cap == 140);
    assert(strcmp(snap.accounts[0].weekly.unit, "h") == 0);
    // cap:null → 纯数值口径
    assert(snap.accounts[0].has_weekly_tokens);
    assert(!snap.accounts[0].weekly_tokens.has_cap);
    assert(snap.accounts[0].weekly_tokens.used == 412000);
    // 次要账户缺的维度为 false
    assert(strcmp(snap.accounts[1].provider, "glm") == 0);
    assert(!snap.accounts[1].has_weekly && !snap.accounts[1].has_rolling5h);
    assert(snap.accounts[1].has_weekly_tokens);
    // 天气
    assert(snap.weather_code == 61);
    assert(strcmp(snap.city, "上海") == 0);
    assert(snap.sunrise_min == 6 * 60 + 12 && snap.sunset_min == 18 * 60 + 5);
    assert(snap.bundle_version == 3);
    printf("parse_sample ok\n");
}

static void test_generated_at_wall_clock(void) {
    int64_t t = 0;
    assert(app_snapshot_parse_iso8601("2026-09-22T06:00:00+08:00", &t));
    // 墙钟数字直接当本地历元:06:00:00
    assert(t % 86400 == 6 * 3600);
    assert(!app_snapshot_parse_iso8601("not-a-date", &t));
    assert(!app_snapshot_parse_iso8601("2026-13-40T99:00:00Z", &t));
    int hhmm = 0;
    assert(app_snapshot_parse_hhmm("06:12") == 372);
    assert(app_snapshot_parse_hhmm("24:00") == -1);
    assert(app_snapshot_parse_hhmm("bad") == -1);
    (void)hhmm;
    printf("generated_at_wall_clock ok\n");
}

static void test_permille_and_offline(void) {
    app_quota_t q = { .used = 62, .cap = 140, .has_cap = true };
    int pm = -1;
    assert(app_snapshot_permille(&q, &pm));
    assert(pm == 443);   // 62/140 = 0.4428…
    q.used = 300;
    assert(app_snapshot_permille(&q, &pm) && pm == 1000);   // 超限收敛
    q.has_cap = false;
    assert(!app_snapshot_permille(&q, &pm));                // 无预算 → 纯数值

    int64_t gen = 3600000LL;
    assert(app_snapshot_offline_hours(gen, gen) == 0);
    assert(app_snapshot_offline_hours(gen, gen + 1) == 1);       // 不足 1h 向上取整
    assert(app_snapshot_offline_hours(gen, gen + 25 * 3600000LL) == 25);
    printf("permille_and_offline ok\n");
}

static void test_fmt(void) {
    char buf[32];
    int64_t now = 1000 * 1000;
    app_fmt_ago(now, now - 30 * 1000, buf, sizeof(buf));
    assert(strcmp(buf, "刚刚") == 0);
    app_fmt_ago(now, now - 5 * 60 * 1000, buf, sizeof(buf));
    assert(strcmp(buf, "5分钟前") == 0);
    app_fmt_ago(now, now - 3 * 3600 * 1000, buf, sizeof(buf));
    assert(strcmp(buf, "3小时前") == 0);
    app_fmt_ago(now, now - 2 * 86400 * 1000, buf, sizeof(buf));
    assert(strcmp(buf, "2天前") == 0);

    app_fmt_tokens(412000, buf, sizeof(buf));
    assert(strcmp(buf, "41.2万") == 0);
    app_fmt_tokens(9800, buf, sizeof(buf));
    assert(strcmp(buf, "9800") == 0);
    app_fmt_tokens(1500000, buf, sizeof(buf));
    assert(strcmp(buf, "150万") == 0);

    app_fmt_clock(25 * 60 * 1000 - 400, buf, sizeof(buf));
    assert(strcmp(buf, "25:00") == 0);
    app_fmt_clock((124 * 60 + 59) * 1000 + 600, buf, sizeof(buf));
    assert(strcmp(buf, "125:00") == 0);
    app_fmt_clock(-5, buf, sizeof(buf));
    assert(strcmp(buf, "0:00") == 0);
    printf("fmt ok\n");
}

static void test_parse_rejects_garbage(void) {
    app_snapshot_t snap;
    assert(!app_snapshot_parse("", 0, &snap));
    assert(!app_snapshot_parse("{", 1, &snap));
    assert(!app_snapshot_parse("{\"schema\":1,\"accounts\":[]}", 28, &snap));
    // 部分坏字段不整体失败:weather 缺失时保持缺省
    const char *partial =
        "{\"schema\":1,\"accounts\":[{\"provider\":\"glm\"}],\"generatedAt\":\"x\"}";
    assert(app_snapshot_parse(partial, strlen(partial), &snap));
    assert(snap.weather_code == 0 && snap.sunrise_min == 0 && snap.sunset_min == 0);
    printf("parse_rejects_garbage ok\n");
}

int main(void) {
    test_parse_sample();
    test_generated_at_wall_clock();
    test_permille_and_offline();
    test_fmt();
    test_parse_rejects_garbage();
    printf("test_app_snapshot: all passed\n");
    return 0;
}

import { test } from "node:test";
import assert from "node:assert/strict";
import { buildSnapshot, computePercent } from "../src/snapshot.js";
import type { CollectorResult, ProviderId } from "../src/collectors/types.js";
import type { WeatherSnapshot } from "../src/weather.js";

const now = new Date("2026-09-22T06:00:00");

const weather: WeatherSnapshot = {
  code: 61,
  kind: "rain",
  sunrise: "06:12",
  sunset: "18:05",
  city: "上海",
  fetchedAt: "2026-09-22T05:59:00+08:00",
};

const claudeOk: CollectorResult = {
  provider: "claude",
  label: "工作号",
  ok: true,
  collectedAt: "2026-09-22T05:55:00+08:00",
  quotas: {
    weekly: { used: 62, cap: 140, unit: "h", resetAt: "2026-09-28T00:00:00+08:00" },
    rolling5h: { used: 18, cap: 36, unit: "h", resetAt: "2026-09-22T09:00:00+08:00" },
    weeklyTokens: { used: 412_000, cap: null, unit: "tokens", resetAt: null },
  },
};

const deepseekBalance: CollectorResult = {
  provider: "deepseek",
  label: "DeepSeek",
  ok: true,
  collectedAt: "2026-09-22T05:55:00+08:00",
  quotas: {
    weeklyTokens: { used: 83.21, cap: null, unit: "CNY", resetAt: null, basis: "balance-remaining" },
  },
};

test("snapshot: schema field-by-field matches spec §4.3", () => {
  const snap = buildSnapshot({
    now,
    results: new Map([["claude", claudeOk]]),
    connected: new Set<ProviderId>(["claude"]),
    weather,
    settings: { primaryAccount: "claude", weeklyTokenBudget: null, badgeName: "Joseph", badgeRole: "Developer" },
    assetBundleVersion: 3,
  });
  assert.equal(snap.schema, 1);
  assert.equal(snap.badgeName, "Joseph");
  assert.equal(snap.badgeRole, "Developer");
  assert.match(snap.generatedAt, /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|[+-]\d{2}:\d{2})$/);
  assert.equal(Date.parse(snap.generatedAt), now.getTime());
  const a = snap.accounts[0]!;
  assert.equal(a.provider, "claude");
  assert.equal(a.label, "工作号");
  assert.equal(a.quotas.weekly!.used, 62);
  assert.equal(a.quotas.weekly!.cap, 140);
  assert.equal(a.quotas.weekly!.unit, "h");
  assert.ok(a.quotas.weekly!.resetAt);
  assert.equal(a.quotas.rolling5h!.used, 18);
  assert.equal(a.quotas.rolling5h!.cap, 36);
  assert.equal(a.quotas.weeklyTokens!.used, 412_000);
  assert.equal(snap.weather!.code, 61);
  assert.equal(snap.weather!.sunrise, "06:12");
  assert.equal(snap.weather!.sunset, "18:05");
  assert.equal(snap.weather!.city, "上海");
  assert.deepEqual(snap.assetBundle, { version: 3 });
});

test("snapshot: primary account is emitted first", () => {
  const snap = buildSnapshot({
    now,
    results: new Map([
      ["claude", claudeOk],
      ["deepseek", deepseekBalance],
    ]),
    connected: new Set<ProviderId>(["claude", "deepseek", "glm"]),
    weather,
    settings: { primaryAccount: "deepseek", weeklyTokenBudget: null },
    assetBundleVersion: 1,
  });
  assert.equal(snap.accounts.length, 3); // all connected providers appear
  assert.equal(snap.accounts[0]!.provider, "deepseek");
  assert.equal(snap.accounts[1]!.provider, "claude");
  assert.equal(snap.accounts[2]!.provider, "glm"); // connected but no result -> null quotas
  assert.deepEqual(snap.accounts[2]!.quotas, { weekly: null, rolling5h: null, weeklyTokens: null });
});

test("snapshot: percentages computed server-side, null without cap", () => {
  const snap = buildSnapshot({
    now,
    results: new Map([
      ["claude", claudeOk],
      ["deepseek", deepseekBalance],
    ]),
    connected: new Set<ProviderId>(["claude", "deepseek"]),
    weather,
    settings: { primaryAccount: "claude", weeklyTokenBudget: 1_000_000 },
    assetBundleVersion: 1,
  });
  const claude = snap.accounts[0]!;
  assert.equal(claude.quotas.weekly!.percent, 44.3); // 62/140
  assert.equal(claude.quotas.rolling5h!.percent, 50); // 18/36
  assert.equal(claude.quotas.weeklyTokens!.cap, 1_000_000); // budget override
  assert.equal(claude.quotas.weeklyTokens!.percent, 41.2); // 412000/1000000
  const ds = snap.accounts[1]!;
  assert.equal(ds.quotas.weekly, null);
  assert.equal(ds.quotas.rolling5h, null);
  assert.equal(ds.quotas.weeklyTokens!.percent, null); // no cap
  assert.equal(ds.quotas.weeklyTokens!.basis, "balance-remaining");
});

test("snapshot: failed collector included with error and null quotas, never breaks the build", () => {
  const failed: CollectorResult = {
    provider: "glm",
    label: "GLM",
    ok: false,
    collectedAt: "2026-09-22T05:55:00+08:00",
    error: "HTTP 401",
  };
  const snap = buildSnapshot({
    now,
    results: new Map([["claude", claudeOk], ["glm", failed]]),
    connected: new Set<ProviderId>(["claude", "glm"]),
    weather: null,
    settings: { primaryAccount: "claude", weeklyTokenBudget: null },
    assetBundleVersion: 2,
  });
  const glm = snap.accounts.find((a) => a.provider === "glm")!;
  assert.equal(glm.error, "HTTP 401");
  assert.deepEqual(glm.quotas, { weekly: null, rolling5h: null, weeklyTokens: null });
  assert.equal(snap.weather, null); // no weather yet -> null, never fabricated
});

test("snapshot: percent math", () => {
  assert.equal(computePercent(62, 140), 44.3);
  assert.equal(computePercent(0, 100), 0);
  assert.equal(computePercent(200, 100), 200);
  assert.equal(computePercent(5, null), null);
  assert.equal(computePercent(null, 100), null);
  assert.equal(computePercent(5, 0), null);
});

test("snapshot: daily total includes all connected accounts regardless of primary", () => {
  const now = new Date(2026, 8, 24, 12);
  const results = new Map<string, CollectorResult>([
    ["claude", { provider: "claude", label: "Claude", ok: true, collectedAt: now.toISOString(), dailyTokens: 832000 }],
    ["glm", { provider: "glm", label: "GLM", ok: true, collectedAt: now.toISOString(), dailyTokens: 100000 }],
    ["deepseek", { provider: "deepseek", label: "DS", ok: true, collectedAt: now.toISOString(), dailyTokens: 99999 }],
  ]);
  const input = { now, results, connected: new Set<"claude" | "glm">(["claude", "glm"]), weather: null,
    settings: { primaryAccount: "glm", weeklyTokenBudget: null }, assetBundleVersion: 1 };
  assert.deepEqual(buildSnapshot(input).dailyTokens, { used: 932000 });
  for (const dailyTokens of [undefined, null, -1, NaN, Infinity]) {
    results.set("glm", { ...results.get("glm")!, dailyTokens });
    assert.deepEqual(buildSnapshot(input).dailyTokens, { used: null });
  }
  results.set("glm", { ...results.get("glm")!, dailyTokens: 0, ok: false });
  assert.equal(buildSnapshot(input).dailyTokens.used, null);
  results.set("glm", { ...results.get("glm")!, dailyTokens: 0, ok: true });
  assert.equal(buildSnapshot(input).dailyTokens.used, 832000);
  assert.equal(buildSnapshot({ ...input, connected: new Set() }).dailyTokens.used, null);
});

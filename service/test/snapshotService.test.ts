import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { SnapshotService } from "../src/snapshotService.js";
import { SettingsStore } from "../src/store/settings.js";
import { WeatherService } from "../src/weather.js";
import type { Collector, CollectorResult } from "../src/collectors/types.js";

test("snapshot service: rejects refresh intervals that would spin or overflow timers", () => {
  for (const refreshMinutes of [0, -1, NaN, Infinity, 0.5, 1441]) {
    assert.throws(() => new SnapshotService({ collectors: [],
      settings: {} as SettingsStore, weather: {} as WeatherService,
      getAssetBundleVersion: () => 1, refreshMinutes }), /SNAPSHOT_REFRESH_MINUTES/);
  }
});

test("snapshot service: cold/stale device requests never wait for collector I/O; refresh coalesces", { timeout: 2000 }, async () => {
  const dir = await mkdtemp(path.join(tmpdir(), "snapshot-cache-"));
  let now = new Date("2026-09-26T02:00:00Z");
  let calls = 0;
  let finish: (r: CollectorResult) => void = () => {};
  let pending = new Promise<CollectorResult>(resolve => { finish = resolve; });
  const collector: Collector = { provider: "local", label: "Local", isConnected: () => true,
    collect: async () => { calls++; return pending; },
    status: () => ({ provider: "local", connected: true, lastRunAt: null, lastOkAt: null, detail: "" }) };
  const service = new SnapshotService({ collectors: [collector], settings: new SettingsStore(dir),
    weather: new WeatherService({ fetchImpl: async () => new Response("", { status: 503 }) }),
    getAssetBundleVersion: () => 1, now: () => now, staleMs: 1000 });
  try {
    assert.equal((await service.get()).dailyTokens.used, null);
    assert.equal(calls, 1);
    const refresh = service.refresh();
    await service.get();
    assert.equal(calls, 1);
    finish({ provider: "local", label: "Local", ok: true, collectedAt: now.toISOString(), dailyTokens: 23 });
    await refresh;
    assert.equal((await service.get()).dailyTokens.used, 23);
    const first = await service.get();
    assert.equal(Date.parse(first.servedAt!), now.getTime());
    pending = new Promise(resolve => { finish = resolve; });
    now = new Date(now.getTime() + 2000);
    const stale = await service.get();
    assert.equal(stale.dailyTokens.used, 23);
    assert.equal(Date.parse(stale.servedAt!), now.getTime());
    assert.equal(Date.parse(stale.generatedAt), now.getTime() - 2000);
    assert.equal(calls, 2);
    finish({ provider: "local", label: "Local", ok: false, collectedAt: now.toISOString(), error: "source unavailable" });
    await service.refresh();
    assert.equal((await service.get()).dailyTokens.used, null);
  } finally { service.stop(); await rm(dir, { recursive: true, force: true }); }
});

test("snapshot service: cached Codex observation expires at reset without waiting for a refresh", async () => {
  const dir = await mkdtemp(path.join(tmpdir(), "quota-cache-"));
  let now = new Date("2026-09-26T02:00:00Z");
  const collector: Collector = { provider: "codex", label: "Codex", isConnected: () => true,
    collect: async () => ({ provider: "codex", label: "Codex", ok: true, collectedAt: now.toISOString(),
      quotas: { weekly: { used: 40, cap: 100, unit: "%", basis: "observed:codex-rate-limit",
        resetAt: "2026-09-26T02:01:00Z" } } }),
    status: () => ({ provider: "codex", connected: true, lastRunAt: null, lastOkAt: null, detail: "" }) };
  const service = new SnapshotService({ collectors: [collector], settings: new SettingsStore(dir),
    weather: new WeatherService({ fetchImpl: async () => new Response("", { status: 503 }) }),
    getAssetBundleVersion: () => 1, now: () => now });
  try {
    await service.refresh();
    assert.equal((await service.get()).accounts[0]?.quotas.weekly?.used, 40);
    now = new Date("2026-09-26T02:01:01Z");
    assert.equal((await service.get()).accounts[0]?.quotas.weekly, null);
  } finally { service.stop(); await rm(dir, { recursive: true, force: true }); }
});

test("snapshot service: midnight hides yesterday and last week's counts until collection completes", async () => {
  const dir = await mkdtemp(path.join(tmpdir(), "midnight-cache-"));
  let now = new Date(2026, 8, 27, 23, 59, 59); // Sunday, local time
  let release: ((result: CollectorResult) => void) | null = null;
  let calls = 0;
  const collector: Collector = { provider: "local", label: "Local", isConnected: () => true,
    collect: async () => { calls++;
      if (calls === 1) return { provider: "local", label: "Local", ok: true,
        collectedAt: now.toISOString(), dailyTokens: 42,
        agentUsage: [{ agent: "pi", dailyTokens: 42, weeklyTokens: 300 }],
        quotas: { weeklyTokens: { used: 300, cap: null, unit: "tokens", resetAt: null } } };
      return new Promise(resolve => { release = resolve; });
    }, status: () => ({ provider: "local", connected: true,
      lastRunAt: null, lastOkAt: null, detail: "" }) };
  const service = new SnapshotService({ collectors: [collector], settings: new SettingsStore(dir),
    weather: new WeatherService({ fetchImpl: async () => new Response("", { status: 503 }) }),
    getAssetBundleVersion: () => 1, now: () => now });
  try {
    await service.refresh();
    assert.equal((await service.get()).dailyTokens.used, 42);
    now = new Date(2026, 8, 28, 0, 0, 1);
    const interim = await service.get();
    assert.equal(interim.dailyTokens.used, null);
    assert.equal(interim.agents[0]?.dailyTokens, null);
    assert.equal(interim.agents[0]?.weeklyTokens, null);
    assert.equal(interim.accounts[0]?.quotas.weeklyTokens, null);
    assert.equal(calls, 2);
    release!({ provider: "local", label: "Local", ok: true,
      collectedAt: now.toISOString(), dailyTokens: 0,
      quotas: { weeklyTokens: { used: 0, cap: null, unit: "tokens", resetAt: null } } });
    await service.refresh();
    assert.equal((await service.get()).dailyTokens.used, 0);
  } finally { service.stop(); await rm(dir, { recursive: true, force: true }); }
});

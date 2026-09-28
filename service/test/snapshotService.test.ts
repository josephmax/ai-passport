import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { SnapshotService } from "../src/snapshotService.js";
import { SettingsStore } from "../src/store/settings.js";
import { WeatherService } from "../src/weather.js";
import type { Collector, CollectorResult } from "../src/collectors/types.js";

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
    pending = new Promise(resolve => { finish = resolve; });
    now = new Date(now.getTime() + 2000);
    assert.equal((await service.get()).dailyTokens.used, 23);
    assert.equal(calls, 2);
    finish({ provider: "local", label: "Local", ok: false, collectedAt: now.toISOString(), error: "source unavailable" });
    await service.refresh();
    assert.equal((await service.get()).dailyTokens.used, null);
  } finally { service.stop(); await rm(dir, { recursive: true, force: true }); }
});

test("snapshot service: cached Codex observation expires at reset without waiting for a refresh", async () => {
  const dir = await mkdtemp(path.join(tmpdir(), "quota-cache-"));
  let now = new Date("2026-09-26T02:00:00Z");
  const collector: Collector = { provider: "chatgpt", label: "Codex", isConnected: () => true,
    collect: async () => ({ provider: "chatgpt", label: "Codex", ok: true, collectedAt: now.toISOString(),
      quotas: { weekly: { used: 40, cap: 100, unit: "%", basis: "observed:codex-rate-limit",
        resetAt: "2026-09-26T02:01:00Z" } } }),
    status: () => ({ provider: "chatgpt", connected: true, lastRunAt: null, lastOkAt: null, detail: "" }) };
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

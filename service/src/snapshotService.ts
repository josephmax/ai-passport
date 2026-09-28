/**
 * SnapshotService: cached snapshot + scheduled refresh. Devices pull hourly;
 * stale reads trigger an asynchronous refresh; device requests never wait for
 * vendor I/O. A background timer refreshes every `refreshMinutes` (default 10).
 */

import { runCollectors } from "./collectors/registry.js";
import type { Collector, ProviderId } from "./collectors/types.js";
import { buildSnapshot, type Snapshot } from "./snapshot.js";
import type { WeatherService } from "./weather.js";
import type { SettingsStore } from "./store/settings.js";
import { resolveLocation } from "./cityTable.js";

export interface SnapshotServiceDeps {
  collectors: Collector[];
  weather: WeatherService;
  settings: SettingsStore;
  getAssetBundleVersion: () => number;
  refreshMinutes?: number;
  staleMs?: number;
  now?: () => Date;
  log?: (msg: string) => void;
}

export class SnapshotService {
  private readonly deps: SnapshotServiceDeps;
  private cache: Snapshot | null = null;
  private cachedAtMs = 0;
  private inflight: Promise<Snapshot> | null = null;
  private timer: NodeJS.Timeout | null = null;
  private readonly staleMs: number;

  constructor(deps: SnapshotServiceDeps) {
    this.deps = deps;
    this.staleMs = deps.staleMs ?? 5 * 60_000;
  }

  start(): void {
    if (this.timer) return;
    const intervalMs = (this.deps.refreshMinutes ?? 10) * 60_000;
    if (!this.cache) void this.refresh().catch((err) => this.log(`initial snapshot refresh failed: ${err}`));
    this.timer = setInterval(() => {
      void this.refresh().catch((err) => this.log(`snapshot refresh failed: ${err}`));
    }, intervalMs);
    this.timer.unref();
  }

  stop(): void {
    if (this.timer) clearInterval(this.timer);
    this.timer = null;
  }

  private log(msg: string): void {
    (this.deps.log ?? console.log)(`[snapshot] ${msg}`);
  }

  async refresh(): Promise<Snapshot> {
    if (this.inflight) return this.inflight;
    this.inflight = this.doRefresh().finally(() => {
      this.inflight = null;
    });
    return this.inflight;
  }

  private async doRefresh(): Promise<Snapshot> {
    const now = this.deps.now ? this.deps.now() : new Date();
    const settings = this.deps.settings.get();
    const loc = resolveLocation(settings.city, settings.customLat, settings.customLon);
    const [results, weather] = await Promise.all([
      runCollectors(this.deps.collectors, now),
      this.deps.weather.get(loc.lat, loc.lon, loc.name),
    ]);
    const connected = new Set<ProviderId>();
    for (const c of this.deps.collectors) {
      if (c.isConnected()) connected.add(c.provider);
    }
    const snapshot = buildSnapshot({
      now,
      results,
      connected,
      weather,
      settings,
      assetBundleVersion: this.deps.getAssetBundleVersion(),
    });
    this.cache = snapshot;
    this.cachedAtMs = now.getTime();
    return snapshot;
  }

  /** Serve immediately, including a truthful initializing snapshot on cold start. */
  async get(): Promise<Snapshot> {
    const nowMs = this.deps.now ? this.deps.now().getTime() : Date.now();
    if (!this.cache || nowMs - this.cachedAtMs >= this.staleMs) {
      void this.refresh().catch(err => this.log(`snapshot refresh failed: ${err}`));
    }
    if (this.cache) {
      return { ...this.cache, accounts: this.cache.accounts.map(account => {
        if (account.provider !== "chatgpt") return account;
        const age = nowMs - Date.parse(account.collectedAt ?? "");
        const quotas = { ...account.quotas };
        for (const kind of ["weekly", "rolling5h"] as const) {
          const quota = quotas[kind];
          if (quota?.basis === "observed:codex-rate-limit" &&
              (!Number.isFinite(age) || age > 60 * 60_000 ||
               (quota.resetAt && Date.parse(quota.resetAt) <= nowMs))) quotas[kind] = null;
        }
        return { ...account, quotas };
      }) };
    }
    const now = new Date(nowMs);
    const connected = new Set(this.deps.collectors.filter(c => c.isConnected()).map(c => c.provider));
    const results = new Map(this.deps.collectors.map(c => [c.provider, {
      provider: c.provider, label: c.label, ok: false, collectedAt: now.toISOString(),
      error: "initial collection in progress",
    }]));
    return buildSnapshot({ now, connected, results, weather: this.deps.weather.cached(),
      settings: this.deps.settings.get(), assetBundleVersion: this.deps.getAssetBundleVersion() });
  }

  peek(): Snapshot | null {
    return this.cache;
  }
}

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
import { localDateKey, localIsoWithOffset, nextLocalMidnight, weekWindow } from "./util/time.js";

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
  private midnightTimer: NodeJS.Timeout | null = null;
  private readonly staleMs: number;
  private readonly refreshMs: number;

  constructor(deps: SnapshotServiceDeps) {
    this.deps = deps;
    this.staleMs = deps.staleMs ?? 5 * 60_000;
    const refreshMinutes = deps.refreshMinutes ?? 10;
    if (!Number.isInteger(refreshMinutes) || refreshMinutes < 1 || refreshMinutes > 1440) {
      throw new Error("SNAPSHOT_REFRESH_MINUTES must be an integer from 1 to 1440");
    }
    this.refreshMs = refreshMinutes * 60_000;
  }

  start(): void {
    if (this.timer) return;
    if (!this.cache) void this.refresh().catch((err) => this.log(`initial snapshot refresh failed: ${err}`));
    this.timer = setInterval(() => {
      void this.refresh().catch((err) => this.log(`snapshot refresh failed: ${err}`));
    }, this.refreshMs);
    this.timer.unref();
    this.scheduleMidnightRefresh();
  }

  stop(): void {
    if (this.timer) clearInterval(this.timer);
    if (this.midnightTimer) clearTimeout(this.midnightTimer);
    this.timer = null;
    this.midnightTimer = null;
  }

  private scheduleMidnightRefresh(): void {
    const now = this.deps.now ? this.deps.now() : new Date();
    const delay = Math.max(1, nextLocalMidnight(now).getTime() - now.getTime());
    this.midnightTimer = setTimeout(() => {
      this.midnightTimer = null;
      void this.refresh().catch(err => this.log(`midnight refresh failed: ${err}`))
        .finally(() => { if (this.timer) this.scheduleMidnightRefresh(); });
    }, delay);
    this.midnightTimer.unref();
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
    const now = this.deps.now ? this.deps.now() : new Date();
    const nowMs = now.getTime();
    const dayChanged = !!this.cache && localDateKey(new Date(this.cachedAtMs)) !== localDateKey(now);
    const weekChanged = !!this.cache &&
      localDateKey(weekWindow(new Date(this.cachedAtMs)).start) !== localDateKey(weekWindow(now).start);
    if (!this.cache || dayChanged || nowMs - this.cachedAtMs >= this.staleMs) {
      void this.refresh().catch(err => this.log(`snapshot refresh failed: ${err}`));
    }
    if (this.cache) {
      return { ...this.cache, servedAt: localIsoWithOffset(now),
        dailyTokens: dayChanged ? { ...this.cache.dailyTokens, used: null } : this.cache.dailyTokens,
        agents: this.cache.agents.map(agent => {
          const age = nowMs - Date.parse(this.cache!.accounts.find(a => a.provider === "codex")?.collectedAt ?? "");
          const staleQuota = (q: typeof agent.weekly) => q &&
            (!Number.isFinite(age) || age > 60 * 60_000 ||
             (q.resetAt && Date.parse(q.resetAt) <= nowMs)) ? null : q;
          return { ...agent, dailyTokens: dayChanged ? null : agent.dailyTokens,
            weeklyTokens: weekChanged ? null : agent.weeklyTokens,
            weekly: staleQuota(agent.weekly), rolling5h: staleQuota(agent.rolling5h) };
        }),
        accounts: this.cache.accounts.map(account => {
        if (account.provider === "local" && weekChanged) return { ...account,
          quotas: { ...account.quotas, weeklyTokens: null } };
        if (account.provider !== "codex") return account;
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
    const connected = new Set(this.deps.collectors.filter(c => c.isConnected()).map(c => c.provider));
    const results = new Map(this.deps.collectors.map(c => [c.provider, {
      provider: c.provider, label: c.label, ok: false, collectedAt: now.toISOString(),
      error: "initial collection in progress",
    }]));
    return { ...buildSnapshot({ now, connected, results, weather: this.deps.weather.cached(),
      settings: this.deps.settings.get(), assetBundleVersion: this.deps.getAssetBundleVersion() }),
      servedAt: localIsoWithOffset(now) };
  }

  peek(): Snapshot | null {
    return this.cache;
  }
}

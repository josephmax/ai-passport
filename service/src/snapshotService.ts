/**
 * SnapshotService: cached snapshot + scheduled refresh. Devices pull hourly;
 * the cache goes stale after `staleMs` (default 5 min) and is rebuilt inline
 * on demand; a background timer refreshes every `refreshMinutes` (default 10).
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
    const intervalMs = (this.deps.refreshMinutes ?? 10) * 60_000;
    void this.refresh().catch((err) => this.log(`initial snapshot refresh failed: ${err}`));
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
    const results = await runCollectors(this.deps.collectors, now);
    const loc = resolveLocation(settings.city, settings.customLat, settings.customLon);
    const weather = await this.deps.weather.get(loc.lat, loc.lon, loc.name);
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

  /** Serve from cache when fresh; otherwise rebuild (concurrent callers share one build). */
  async get(): Promise<Snapshot> {
    const nowMs = this.deps.now ? this.deps.now().getTime() : Date.now();
    if (this.cache && nowMs - this.cachedAtMs < this.staleMs) return this.cache;
    return this.refresh();
  }

  peek(): Snapshot | null {
    return this.cache;
  }
}

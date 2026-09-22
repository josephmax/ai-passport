/**
 * Snapshot aggregation — output schema matches the main spec §4.3 field by
 * field (extra additive fields: `percent`, `kind`, `basis`, `error`).
 *
 * Percentages are always computed SERVER-SIDE (spec §4.3): each quota carries
 * `percent` (0-100, one decimal; null when there is no cap). The primary
 * account is emitted first — the device renders the first entry on the main
 * dashboard screen.
 */

import type { CollectorResult, QuotaReading, ProviderId } from "./collectors/types.js";
import type { WeatherSnapshot } from "./weather.js";
import type { ServiceSettings } from "./store/settings.js";
import { localIsoWithOffset } from "./util/time.js";

export interface SnapshotQuota {
  used: number | null;
  cap: number | null;
  unit: string;
  resetAt: string | null;
  /** Server-computed percentage (one decimal), null when cap is absent. */
  percent: number | null;
  basis?: string;
}

export interface SnapshotAccount {
  provider: ProviderId;
  label: string;
  quotas: {
    weekly: SnapshotQuota | null;
    rolling5h: SnapshotQuota | null;
    weeklyTokens: SnapshotQuota | null;
  };
  error?: string;
}

export interface Snapshot {
  schema: 1;
  generatedAt: string;
  accounts: SnapshotAccount[];
  weather: {
    code: number;
    kind: string;
    sunrise: string;
    sunset: string;
    city: string;
  } | null;
  assetBundle: { version: number };
}

export function computePercent(used: number | null, cap: number | null): number | null {
  if (used === null || cap === null || cap <= 0) return null;
  return Math.round((used / cap) * 1000) / 10;
}

function toSnapshotQuota(
  q: QuotaReading | undefined,
  capOverride?: number | null,
): SnapshotQuota | null {
  if (!q) return null;
  const cap = capOverride !== undefined ? capOverride : q.cap;
  return {
    used: q.used,
    cap,
    unit: q.unit,
    resetAt: q.resetAt,
    percent: computePercent(q.used, cap),
    ...(q.basis !== undefined ? { basis: q.basis } : {}),
  };
}

const PROVIDER_ORDER: ProviderId[] = ["claude", "glm", "deepseek", "chatgpt"];

/**
 * Build a snapshot from collector results. Pure apart from `now`.
 *
 * - `results` keyed by provider; only CONNECTED providers appear.
 * - A connected provider whose collection failed still appears (quotas null,
 *   `error` set) so the device can show the account as unreachable rather
 *   than silently dropping it.
 * - Primary account is moved to index 0 when present.
 */
export function buildSnapshot(input: {
  now: Date;
  results: Map<string, CollectorResult>;
  connected: Set<ProviderId>;
  weather: WeatherSnapshot | null;
  settings: Pick<ServiceSettings, "primaryAccount" | "weeklyTokenBudget">;
  assetBundleVersion: number;
}): Snapshot {
  const { now, results, connected, weather, settings, assetBundleVersion } = input;
  const accounts: SnapshotAccount[] = [];
  for (const provider of PROVIDER_ORDER) {
    if (!connected.has(provider)) continue;
    const r = results.get(provider);
    const label = r?.label ?? provider;
    const quotas = {
      weekly: toSnapshotQuota(r?.quotas?.weekly),
      rolling5h: toSnapshotQuota(r?.quotas?.rolling5h),
      weeklyTokens:
        provider === "claude"
          ? toSnapshotQuota(r?.quotas?.weeklyTokens, settings.weeklyTokenBudget)
          : toSnapshotQuota(r?.quotas?.weeklyTokens),
    };
    const account: SnapshotAccount = { provider, label, quotas };
    if (r && !r.ok && r.error) account.error = r.error;
    accounts.push(account);
  }
  // primary first (device renders accounts[0] on the main dashboard)
  const primaryIdx = accounts.findIndex((a) => a.provider === settings.primaryAccount);
  if (primaryIdx > 0) {
    const [primary] = accounts.splice(primaryIdx, 1);
    accounts.unshift(primary!);
  }
  return {
    schema: 1,
    generatedAt: localIsoWithOffset(now),
    accounts,
    weather: weather
      ? {
          code: weather.code,
          kind: weather.kind,
          sunrise: weather.sunrise,
          sunset: weather.sunset,
          city: weather.city,
        }
      : null,
    assetBundle: { version: assetBundleVersion },
  };
}

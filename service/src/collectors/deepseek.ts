/**
 * DeepSeek collector: official balance endpoint
 * `GET https://api.deepseek.com/user/balance` (Bearer key).
 *
 * Monetary balance is retained separately from token quotas.
 */

import type { Collector, CollectorResult, CollectorStatus } from "./types.js";
import { localIsoWithOffset } from "../util/time.js";

export const DEFAULT_DEEPSEEK_BALANCE_URL = "https://api.deepseek.com/user/balance";

export interface DeepSeekBalance {
  available: boolean;
  currency: string;
  remaining: number;
  granted: number;
  toppedUp: number;
}

/** Pure response mapper (unit-tested). */
export function mapDeepSeekBalance(json: unknown): DeepSeekBalance | null {
  if (typeof json !== "object" || json === null) return null;
  const root = json as Record<string, unknown>;
  const infos = root.balance_infos;
  if (!Array.isArray(infos) || infos.length === 0) return null;
  // prefer CNY
  let pick: Record<string, unknown> | undefined;
  for (const info of infos) {
    if (typeof info === "object" && info !== null && (info as Record<string, unknown>).currency === "CNY") {
      pick = info as Record<string, unknown>;
      break;
    }
  }
  if (!pick) {
    const first = infos[0];
    if (typeof first === "object" && first !== null) pick = first as Record<string, unknown>;
  }
  if (!pick) return null;
  const toNum = (v: unknown): number | null => {
    const n = typeof v === "string" && v.trim() !== "" ? Number(v) : typeof v === "number" ? v : NaN;
    return Number.isFinite(n) ? n : null;
  };
  const remaining = toNum(pick.total_balance);
  if (remaining === null || remaining < 0 || typeof pick.currency !== "string" || !/^[A-Z]{3}$/.test(pick.currency)) return null;
  return {
    available: root.is_available === true,
    currency: pick.currency,
    remaining,
    granted: toNum(pick.granted_balance) ?? 0,
    toppedUp: toNum(pick.topped_up_balance) ?? 0,
  };
}

export class DeepSeekCollector implements Collector {
  readonly provider = "deepseek" as const;
  readonly label: string;
  private readonly url: string;
  private readonly getKey: () => string | null;
  private lastRunAt: Date | null = null;
  private lastOkAt: Date | null = null;
  private lastError: string | null = null;

  constructor(opts: { label?: string; url?: string; getKey: () => string | null }) {
    this.label = opts.label ?? "DeepSeek";
    this.url = opts.url ?? DEFAULT_DEEPSEEK_BALANCE_URL;
    this.getKey = opts.getKey;
  }

  isConnected(): boolean {
    const k = this.getKey();
    return typeof k === "string" && k.trim() !== "";
  }

  async collect(now: Date): Promise<CollectorResult> {
    this.lastRunAt = now;
    const base: CollectorResult = {
      provider: this.provider,
      label: this.label,
      ok: false,
      collectedAt: localIsoWithOffset(now),
    };
    const key = this.getKey();
    if (!key) {
      this.lastError = "no API key";
      return { ...base, error: this.lastError };
    }
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), 10_000);
    try {
      const res = await fetch(this.url, {
        headers: { Authorization: `Bearer ${key}`, Accept: "application/json" },
        signal: ctrl.signal,
      });
      if (!res.ok) {
        this.lastError = `HTTP ${res.status}`;
        return { ...base, error: this.lastError };
      }
      const json: unknown = await res.json();
      const balance = mapDeepSeekBalance(json);
      if (!balance) {
        this.lastError = "unrecognized balance response";
        return { ...base, error: this.lastError };
      }
      this.lastOkAt = now;
      this.lastError = null;
      return {
        ...base,
        ok: true,
        balance: { remaining: balance.remaining, currency: balance.currency,
          basis: "api:deepseek-balance" },
      };
    } catch (err) {
      this.lastError = err instanceof Error ? err.message : String(err);
      return { ...base, error: this.lastError };
    } finally {
      clearTimeout(timer);
    }
  }

  status(): CollectorStatus {
    return {
      provider: this.provider,
      connected: this.isConnected(),
      lastRunAt: this.lastRunAt ? localIsoWithOffset(this.lastRunAt) : null,
      lastOkAt: this.lastOkAt ? localIsoWithOffset(this.lastOkAt) : null,
      detail: this.lastError ?? (this.lastOkAt ? "采集正常" : "尚未采集"),
    };
  }
}

/**
 * GLM (Zhipu bigmodel.cn) collector — best effort.
 *
 * P1 status: no officially documented public "coding plan quota" endpoint.
 * We try the community-known coding-plan resource endpoint (URL configurable
 * in data/config.json) and map the response **heuristically but honestly**:
 * numbers are extracted only when a recognizable used/total (or remaining)
 * pair exists; anything unrecognized degrades to ok:false — we never invent
 * quota values. Response shapes seen in the wild include
 * `{data:{usageHours,totalHours}}`, `{data:{usedPrompts,totalPrompts}}` and
 * simple `{data:{remaining,...}}` balances.
 */

import type { Collector, CollectorResult, CollectorStatus } from "./types.js";
import { localIsoWithOffset } from "../util/time.js";

export const DEFAULT_GLM_CODING_PLAN_URL =
  "https://open.bigmodel.cn/api/paas/openapi/resource/coding-plan";

export interface GlmExtract {
  kind: "hours" | "prompts" | "remaining";
  used: number | null;
  total: number | null;
  remaining: number | null;
}

const USED_KEYS = /^(used|usage|used_?hours|usage_?hours|used_?prompts)$/i;
const TOTAL_KEYS = /^(total|total_?hours|limit|cap|total_?prompts)$/i;
const REMAIN_KEYS = /^(remaining|remain|left|available_?hours)$/i;

function isNum(v: unknown): v is number {
  return typeof v === "number" && Number.isFinite(v);
}

/**
 * Recursively look for a recognizable usage/total pair or a remaining value.
 * Pure; unit-tested against several observed shapes.
 */
export function mapGlmResponse(json: unknown): GlmExtract | null {
  const seen = new Set<unknown>();
  const visit = (node: unknown): GlmExtract | null => {
    if (typeof node !== "object" || node === null) return null;
    if (seen.has(node)) return null;
    seen.add(node);
    const obj = node as Record<string, unknown>;
    let used: number | null = null;
    let total: number | null = null;
    let remaining: number | null = null;
    for (const [k, v] of Object.entries(obj)) {
      if (!isNum(v)) continue;
      if (USED_KEYS.test(k)) used = v;
      else if (TOTAL_KEYS.test(k)) total = v;
      else if (REMAIN_KEYS.test(k)) remaining = v;
    }
    if (used !== null && total !== null) {
      const hours = /hours?/i.test(Object.keys(obj).join(" "));
      const prompts = /prompts?/i.test(Object.keys(obj).join(" "));
      return { kind: hours ? "hours" : prompts ? "prompts" : "hours", used, total, remaining };
    }
    if (remaining !== null) {
      return { kind: "remaining", used: null, total: null, remaining };
    }
    for (const v of Object.values(obj)) {
      const found = visit(v);
      if (found) return found;
    }
    return null;
  };
  return visit(json);
}

export class GlmCollector implements Collector {
  readonly provider = "glm" as const;
  readonly label: string;
  private readonly url: string;
  private readonly getKey: () => string | null;
  private lastRunAt: Date | null = null;
  private lastOkAt: Date | null = null;
  private lastError: string | null = null;

  constructor(opts: { label?: string; url?: string; getKey: () => string | null }) {
    this.label = opts.label ?? "GLM";
    this.url = opts.url ?? DEFAULT_GLM_CODING_PLAN_URL;
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
      const mapped = mapGlmResponse(json);
      if (!mapped) {
        this.lastError = "unrecognized response shape (quota values not extracted)";
        return { ...base, error: this.lastError };
      }
      this.lastOkAt = now;
      this.lastError = null;
      if (mapped.kind === "hours" && mapped.used !== null && mapped.total !== null) {
        return {
          ...base,
          ok: true,
          quotas: {
            // GLM coding plans are 5h-window based -> rolling5h slot
            rolling5h: {
              used: mapped.used,
              cap: mapped.total,
              unit: "h",
              resetAt: null,
              basis: "api:glm-coding-plan",
            },
          },
        };
      }
      if (mapped.kind === "prompts" && mapped.used !== null && mapped.total !== null) {
        return {
          ...base,
          ok: true,
          quotas: {
            rolling5h: {
              used: mapped.used,
              cap: mapped.total,
              unit: "tokens",
              resetAt: null,
              basis: "api:glm-coding-plan-prompts",
            },
          },
        };
      }
      // remaining balance only
      return {
        ...base,
        ok: true,
        quotas: {
          weeklyTokens: {
            used: mapped.remaining,
            cap: null,
            unit: "CNY",
            resetAt: null,
            basis: "balance-remaining",
          },
        },
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

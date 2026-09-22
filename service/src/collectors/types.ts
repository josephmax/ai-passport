/** Collector plugin contracts. One failing collector must never break the snapshot. */

export type ProviderId = "claude" | "glm" | "deepseek" | "chatgpt";

export interface QuotaReading {
  used: number | null;
  cap: number | null;
  unit: "h" | "tokens" | "CNY";
  /** RFC3339 local-offset string, or null when the upstream gives no reset time. */
  resetAt: string | null;
  /** Honest labeling for degraded providers, e.g. "balance-remaining". */
  basis?: string;
}

export interface CollectorResult {
  provider: ProviderId;
  label: string;
  ok: boolean;
  collectedAt: string;
  error?: string;
  quotas?: {
    weekly?: QuotaReading;
    rolling5h?: QuotaReading;
    weeklyTokens?: QuotaReading;
  };
}

export interface CollectorStatus {
  provider: ProviderId;
  connected: boolean;
  lastRunAt: string | null;
  lastOkAt: string | null;
  detail: string;
}

export interface Collector {
  readonly provider: ProviderId;
  readonly label: string;
  /** Whether the account is configured (log path present / API key set). */
  isConnected(): boolean;
  collect(now: Date): Promise<CollectorResult>;
  status(): CollectorStatus;
}

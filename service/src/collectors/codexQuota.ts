/** Read provider quota observations already captured by the local Codex client.
 * No credentials, chat contents, or bill estimates are returned.
 */
import { existsSync } from "node:fs";
import { readdir, stat, open } from "node:fs/promises";
import { homedir } from "node:os";
import path from "node:path";
import type { Collector, CollectorResult, CollectorStatus, QuotaReading } from "./types.js";
import { localIsoWithOffset } from "../util/time.js";

export const QUOTA_MAX_AGE_MS = 60 * 60_000;
const TAIL_BYTES = 1024 * 1024;
interface Sample { observedAt: string; quotas: NonNullable<CollectorResult["quotas"]> }

export function parseCodexQuota(lines: string, now: Date): Sample | null {
  let latest: Sample | null = null;
  for (const line of lines.split("\n")) {
    try {
      const event = JSON.parse(line);
      if (event.type !== "event_msg" || event.payload?.type !== "token_count") continue;
      const at = Date.parse(event.timestamp);
      if (!Number.isFinite(at) || at > now.getTime() || now.getTime() - at > QUOTA_MAX_AGE_MS) continue;
      const rate = event.payload.rate_limits;
      if (!rate || (rate.limit_id && rate.limit_id !== "codex")) continue;
      const quotas: Sample["quotas"] = {};
      for (const window of [rate.primary, rate.secondary]) {
        if (!window) continue;
        const { used_percent: used, window_minutes: minutes, resets_at: reset } = window;
        if (typeof used !== "number" || !Number.isFinite(used) || used < 0 || used > 100 ||
          typeof minutes !== "number" || !Number.isFinite(minutes)) continue;
        if (typeof reset !== "number" || !Number.isFinite(reset) || reset * 1000 <= now.getTime() ||
          !Number.isFinite(new Date(reset * 1000).getTime())) continue;
        const reading: QuotaReading = { used, cap: 100, unit: "%",
          resetAt: localIsoWithOffset(new Date(reset * 1000)), basis: "observed:codex-rate-limit" };
        if (minutes >= 299 && minutes <= 301) quotas.rolling5h = reading;
        else if (minutes >= 10079 && minutes <= 10081) quotas.weekly = reading;
      }
      // A newer empty sample invalidates older windows (plan/account may change).
      if (!latest || at >= Date.parse(latest.observedAt)) latest = { observedAt: new Date(at).toISOString(), quotas };
    } catch { /* Partial last line, unrelated records, and rotated files are expected. */ }
  }
  return latest;
}

async function readRecentSamples(root: string): Promise<string[]> {
  // Use the dated directory structure; only descend into the newest seven dates.
  const directories: string[] = [];
  for (const year of (await readdir(root)).filter(s => /^\d{4}$/.test(s)).sort().reverse()) {
    for (const month of (await readdir(path.join(root, year))).filter(s => /^\d{2}$/.test(s)).sort().reverse()) {
      for (const day of (await readdir(path.join(root, year, month))).filter(s => /^\d{2}$/.test(s)).sort().reverse()) {
        directories.push(path.join(root, year, month, day));
        if (directories.length === 7) break;
      }
      if (directories.length === 7) break;
    }
    if (directories.length === 7) break;
  }
  const files: { file: string; mtime: number }[] = [];
  for (const dir of directories) {
    for (const name of await readdir(dir)) {
      if (!name.endsWith(".jsonl")) continue;
      const file = path.join(dir, name);
      const info = await stat(file).catch(() => null);
      if (info?.isFile()) files.push({ file, mtime: info.mtimeMs });
    }
  }
  const tails: string[] = [];
  for (const { file } of files.sort((a, b) => b.mtime - a.mtime).slice(0, 32)) {
    const handle = await open(file, "r").catch(() => null);
    if (!handle) continue;
    try {
      const { size } = await handle.stat();
      const offset = Math.max(0, size - TAIL_BYTES);
      const buffer = Buffer.alloc(Math.min(size, TAIL_BYTES));
      const { bytesRead } = await handle.read(buffer, 0, buffer.length, offset);
      const text = buffer.subarray(0, bytesRead).toString("utf8");
      tails.push(offset ? text.slice(text.indexOf("\n") + 1) : text);
    } finally { await handle.close(); }
  }
  return tails;
}

export class CodexQuotaCollector implements Collector {
  readonly provider = "codex" as const;
  readonly label = "Codex quota";
  private lastRunAt: string | null = null;
  private lastOkAt: string | null = null;
  private detail = "尚未采样";
  constructor(private readonly root = path.join(process.env.CODEX_HOME ?? path.join(homedir(), ".codex"), "sessions"),
    private readonly read = () => readRecentSamples(root)) {}
  isConnected(): boolean { return existsSync(this.root); }
  async collect(now: Date): Promise<CollectorResult> {
    this.lastRunAt = now.toISOString();
    const base = { provider: this.provider, label: this.label, collectedAt: this.lastRunAt };
    try {
      const samples = (await this.read()).map(text => parseCodexQuota(text, now)).filter(s => s !== null);
      const sample = samples.sort((a, b) => Date.parse(b.observedAt) - Date.parse(a.observedAt))[0];
      if (!sample || !Object.keys(sample.quotas).length) throw new Error("没有一小时内且尚未重置的 Codex 额度样本；运行 Codex 后刷新");
      this.lastOkAt = sample.observedAt;
      this.detail = "本机 Codex 会话观测额度（未绑定登录身份，非账单），最长保留一小时";
      return { ...base, ok: true, collectedAt: sample.observedAt, quotas: sample.quotas };
    } catch {
      this.detail = "没有可用的近期 Codex 额度样本；运行 Codex 后刷新";
      return { ...base, ok: false, error: this.detail };
    }
  }
  status(): CollectorStatus { return { provider: this.provider, connected: this.isConnected(),
    lastRunAt: this.lastRunAt, lastOkAt: this.lastOkAt, detail: this.detail }; }
}

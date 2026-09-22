/**
 * Claude collector: reads Claude Code session logs under
 * `~/.claude/projects` (all "*.jsonl" recursively, ccusage mode). Pure aggregation lives in `claudeUsage.ts`; this file only
 * does filesystem walking and wiring.
 */

import { statSync, createReadStream } from "node:fs";
import type { Dirent } from "node:fs";
import { opendir } from "node:fs/promises";
import path from "node:path";
import os from "node:os";
import readline from "node:readline";
import {
  aggregateClaudeUsage,
  parseUsageEntry,
  type UsageEntry,
} from "./claudeUsage.js";
import type { Collector, CollectorResult, CollectorStatus } from "./types.js";
import { localIsoWithOffset } from "../util/time.js";

export interface ClaudeCollectorOptions {
  /** Directory containing `<project>/<session>.jsonl`. Default ~/.claude/projects. */
  projectsPath?: string | null;
  label?: string;
  caps: { weeklyCapHours: number; rolling5hCapHours: number };
  /** Max jsonl bytes scanned per refresh, defensive cap (default 64 MB). */
  maxTotalBytes?: number;
}

async function* walkJsonl(dir: string): AsyncGenerator<string> {
  let handle: AsyncIterable<Dirent>;
  try {
    handle = await opendir(dir);
  } catch {
    return; // missing dir -> nothing to read
  }
  for await (const e of handle as AsyncIterable<Dirent>) {
    const full = path.join(dir, e.name);
    if (e.isDirectory()) {
      yield* walkJsonl(full);
    } else if (e.isFile() && e.name.endsWith(".jsonl")) {
      yield full;
    }
  }
}

export class ClaudeCollector implements Collector {
  readonly provider = "claude" as const;
  readonly label: string;
  private readonly opts: ClaudeCollectorOptions;
  private lastRunAt: Date | null = null;
  private lastOkAt: Date | null = null;
  private lastError: string | null = null;
  private lastFileCount = 0;
  private lastEntryCount = 0;

  constructor(opts: ClaudeCollectorOptions) {
    this.opts = opts;
    this.label = opts.label ?? "Claude";
  }

  private resolvedPath(): string {
    const p = this.opts.projectsPath && this.opts.projectsPath.trim() !== ""
      ? this.opts.projectsPath
      : path.join(os.homedir(), ".claude", "projects");
    return p.startsWith("~") ? path.join(os.homedir(), p.slice(1)) : p;
  }

  projectsDir(): string {
    return this.resolvedPath();
  }

  isConnected(): boolean {
    // The Claude collector is always "configured" (it reads local logs).
    // If the directory is missing, collect() reports ok:false with the reason.
    try {
      const st = statSync(this.resolvedPath());
      return st.isDirectory();
    } catch {
      return false;
    }
  }

  async collect(now: Date): Promise<CollectorResult> {
    this.lastRunAt = now;
    const base: CollectorResult = {
      provider: this.provider,
      label: this.label,
      ok: false,
      collectedAt: localIsoWithOffset(now),
    };
    const root = this.resolvedPath();
    const entries: UsageEntry[] = [];
    let fileCount = 0;
    let scanned = 0;
    const maxBytes = this.opts.maxTotalBytes ?? 64 * 1024 * 1024;
    try {
      for await (const file of walkJsonl(root)) {
        fileCount++;
        scanned += statSync(file).size;
        if (scanned > maxBytes) break;
        const rl = readline.createInterface({
          input: createReadStream(file, { encoding: "utf8" }),
          crlfDelay: Infinity,
        });
        for await (const line of rl) {
          const e = parseUsageEntry(line);
          if (e) entries.push(e);
        }
      }
    } catch (err) {
      this.lastError = err instanceof Error ? err.message : String(err);
      return { ...base, error: this.lastError };
    }
    if (fileCount === 0) {
      this.lastError = `no .jsonl session logs under ${root}`;
      return { ...base, error: this.lastError };
    }
    const stats = aggregateClaudeUsage(entries, now, this.opts.caps);
    this.lastOkAt = now;
    this.lastError = null;
    this.lastFileCount = fileCount;
    this.lastEntryCount = entries.length;
    return {
      ...base,
      ok: true,
      quotas: {
        weekly: {
          used: stats.weeklyHours,
          cap: this.opts.caps.weeklyCapHours,
          unit: "h",
          resetAt: localIsoWithOffset(stats.weeklyResetAt),
        },
        rolling5h: {
          used: stats.rolling5hHours,
          cap: this.opts.caps.rolling5hCapHours,
          unit: "h",
          resetAt: stats.rolling5hResetAt ? localIsoWithOffset(stats.rolling5hResetAt) : null,
        },
        weeklyTokens: {
          // cap comes from the portal preference (weekly token budget) at snapshot time
          used: stats.weeklyTokens,
          cap: null,
          unit: "tokens",
          resetAt: localIsoWithOffset(stats.weeklyResetAt),
        },
      },
    };
  }

  status(): CollectorStatus {
    return {
      provider: this.provider,
      connected: this.isConnected(),
      lastRunAt: this.lastRunAt ? localIsoWithOffset(this.lastRunAt) : null,
      lastOkAt: this.lastOkAt ? localIsoWithOffset(this.lastOkAt) : null,
      detail: this.lastError
        ? this.lastError
        : this.lastOkAt
          ? `日志 ${this.lastFileCount} 个文件 / ${this.lastEntryCount} 条用量记录`
          : "尚未采集",
    };
  }
}

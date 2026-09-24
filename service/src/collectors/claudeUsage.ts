/**
 * Pure Claude Code usage aggregation (ccusage-style, simplified).
 *
 * Input: parsed entries from Claude Code session logs (all "*.jsonl" under
 * `~/.claude/projects`) — each line is a
 * JSON object; we use lines that carry `timestamp` plus
 * `message.usage.{input_tokens,output_tokens}`.
 *
 * Usage-hours model (documented simplification of ccusage's usage-hours):
 *   - Per session file, message timestamps are clustered into activity blocks;
 *     a gap > `gapMinutes` (default 5) splits blocks.
 *   - A block's duration = last_ts - first_ts within the block.
 *   - Hours in a window = sum of block durations clipped to that window.
 *   - Rolling 5h window: opens at the start of the most recent activity block
 *     that still overlaps the last 5 hours; window length is 5h.
 *
 * Tokens: weekly token usage sums input+output tokens (cache tokens excluded,
 * matching ccusage's default mode).
 */

import { weekWindow } from "../util/time.js";

export interface UsageEntry {
  ts: number; // epoch ms
  inputTokens: number;
  outputTokens: number;
}

export interface ActivityBlock {
  start: number;
  end: number;
}

/** Parse one jsonl line; returns null for lines without timestamp+usage. */
export function parseUsageEntry(line: string): UsageEntry | null {
  const trimmed = line.trim();
  if (!trimmed) return null;
  let o: unknown;
  try {
    o = JSON.parse(trimmed);
  } catch {
    return null;
  }
  if (typeof o !== "object" || o === null) return null;
  const obj = o as Record<string, unknown>;
  const tsRaw = obj.timestamp;
  if (typeof tsRaw !== "string") return null;
  const ts = Date.parse(tsRaw);
  if (!Number.isFinite(ts)) return null;
  const msg = obj.message;
  if (typeof msg !== "object" || msg === null) return null;
  const usage = (msg as Record<string, unknown>).usage;
  if (typeof usage !== "object" || usage === null) return null;
  const u = usage as Record<string, unknown>;
  const input = Number(u.input_tokens ?? 0);
  const output = Number(u.output_tokens ?? 0);
  if (!Number.isFinite(input) || !Number.isFinite(output)) return null;
  if (input === 0 && output === 0) return null;
  return { ts, inputTokens: input, outputTokens: output };
}

/** Cluster timestamps into activity blocks (ms), sorted. */
export function activityBlocks(timestamps: number[], gapMinutes = 5): ActivityBlock[] {
  if (timestamps.length === 0) return [];
  const sorted = [...timestamps].sort((a, b) => a - b);
  const gapMs = gapMinutes * 60_000;
  const blocks: ActivityBlock[] = [];
  let start = sorted[0]!;
  let end = sorted[0]!;
  for (let i = 1; i < sorted.length; i++) {
    const t = sorted[i]!;
    if (t - end <= gapMs) {
      end = t;
    } else {
      blocks.push({ start, end });
      start = t;
      end = t;
    }
  }
  blocks.push({ start, end });
  return blocks;
}

/** Sum of block durations (hours) clipped to [winStart, winEnd]. */
export function hoursInWindow(
  blocks: ActivityBlock[],
  winStart: number,
  winEnd: number,
): number {
  let ms = 0;
  for (const b of blocks) {
    const s = Math.max(b.start, winStart);
    const e = Math.min(b.end, winEnd);
    if (e > s) ms += e - s;
  }
  return ms / 3_600_000;
}

export interface ClaudeUsageStats {
  weeklyHours: number;
  weeklyResetAt: Date;
  rolling5hHours: number;
  /** End of the active 5h window, or null when no recent activity. */
  rolling5hResetAt: Date | null;
  weeklyTokens: number;
  dailyTokens: number;
  blockCount: number;
}

export interface ClaudeCaps {
  weeklyCapHours: number;
  rolling5hCapHours: number;
}

const round1 = (x: number): number => Math.round(x * 10) / 10;

export function aggregateClaudeUsage(
  entries: UsageEntry[],
  now: Date,
  _caps: ClaudeCaps,
  gapMinutes = 5,
): ClaudeUsageStats {
  const nowMs = now.getTime();
  const week = weekWindow(now);
  const blocks = activityBlocks(entries.map((e) => e.ts), gapMinutes);

  const weeklyHours = round1(hoursInWindow(blocks, week.start.getTime(), nowMs));

  let weeklyTokens = 0;
  let dailyTokens = 0;
  const midnight = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();
  for (const e of entries) {
    if (e.ts >= midnight && e.ts <= nowMs) dailyTokens += e.inputTokens + e.outputTokens;
    if (e.ts >= week.start.getTime() && e.ts <= nowMs) {
      weeklyTokens += e.inputTokens + e.outputTokens;
    }
  }

  const FIVE_H = 5 * 3_600_000;
  const last = blocks.length > 0 ? blocks[blocks.length - 1]! : null;
  let rolling5hHours = 0;
  let rolling5hResetAt: Date | null = null;
  if (last && last.end >= nowMs - FIVE_H) {
    const winStart = last.start;
    const winEnd = winStart + FIVE_H;
    rolling5hHours = round1(hoursInWindow(blocks, winStart, Math.min(winEnd, nowMs)));
    rolling5hResetAt = new Date(winEnd);
  }

  return {
    weeklyHours,
    weeklyResetAt: week.end,
    rolling5hHours,
    rolling5hResetAt,
    weeklyTokens,
    dailyTokens,
    blockCount: blocks.length,
  };
}

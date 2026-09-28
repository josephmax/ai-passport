/** Unified local coding-agent usage via the pinned ccusage CLI. */
import { execFile } from "node:child_process";
import { createRequire } from "node:module";
import { promisify } from "node:util";
import type { Collector, CollectorResult, CollectorStatus } from "./types.js";
import { localDateKey, localIsoWithOffset, weekWindow } from "../util/time.js";

const execFileAsync = promisify(execFile);
const require = createRequire(import.meta.url);

/** Resolve through Node's package lookup, independent of src/dist or hoisting. */
function resolveCliPath(): string | null {
  try { return require.resolve("ccusage/src/cli.js"); }
  catch { return null; }
}

type Runner = (args: string[]) => Promise<string>;

function tokens(value: unknown): number | null {
  return typeof value === "number" && Number.isSafeInteger(value) && value >= 0 ? value : null;
}

export interface LocalUsage {
  dailyTokens: number;
  weeklyTokens: number;
  agents: string[];
  agentUsage: { agent: string; dailyTokens: number; weeklyTokens: number }[];
}

/** A missing today row means zero today only when another row proves a source exists. */
export function parseLocalUsage(json: unknown, now: Date): LocalUsage | null {
  if (typeof json !== "object" || json === null) return null;
  const rows = (json as { daily?: unknown }).daily;
  if (!Array.isArray(rows) || rows.length === 0) return null;
  const today = localDateKey(now);
  const weekStart = localDateKey(weekWindow(now).start);
  let dailyTokens = 0;
  let weeklyTokens = 0;
  const agents = new Set<string>();
  const agentUsage = new Map<string, { agent: string; dailyTokens: number; weeklyTokens: number }>();
  for (const row of rows) {
    if (typeof row !== "object" || row === null) return null;
    const r = row as { period?: unknown; totalTokens?: unknown; agents?: unknown };
    if (typeof r.period !== "string" || r.period < weekStart || r.period > today) return null;
    const count = tokens(r.totalTokens);
    if (count === null) return null;
    weeklyTokens += count;
    if (r.period === today) dailyTokens += count;
    if (Array.isArray(r.agents)) {
      for (const agent of r.agents) {
        if (typeof agent?.agent !== "string") continue;
        agents.add(agent.agent);
        const value = tokens(agent.totalTokens);
        if (value === null) continue;
        const previous = agentUsage.get(agent.agent) ?? { agent: agent.agent, dailyTokens: 0, weeklyTokens: 0 };
        previous.weeklyTokens += value;
        if (r.period === today) previous.dailyTokens += value;
        agentUsage.set(agent.agent, previous);
      }
    }
  }
  if (!Number.isSafeInteger(weeklyTokens) || !Number.isSafeInteger(dailyTokens) ||
      [...agentUsage.values()].some(a => !Number.isSafeInteger(a.weeklyTokens))) return null;
  return { dailyTokens, weeklyTokens, agents: [...agents].sort(),
    agentUsage: [...agentUsage.values()].sort((a, b) => b.dailyTokens - a.dailyTokens || a.agent.localeCompare(b.agent)) };
}

export class LocalAgentsCollector implements Collector {
  readonly provider = "local" as const;
  readonly label = "Local Agents";
  private lastRunAt: Date | null = null;
  private lastOkAt: Date | null = null;
  private detail = "尚未采集";

  constructor(private readonly run: Runner = async (args) => {
    const cliPath = resolveCliPath();
    if (!cliPath) throw new Error("ccusage 未安装或入口不可解析");
    const { stdout } = await execFileAsync(process.execPath, [cliPath, ...args], {
      timeout: 12_000, maxBuffer: 4 * 1024 * 1024,
    });
    return stdout;
  }) {}

  isConnected(): boolean { return resolveCliPath() !== null; }

  async collect(now: Date): Promise<CollectorResult> {
    this.lastRunAt = now;
    const base: CollectorResult = { provider: this.provider, label: this.label,
      ok: false, collectedAt: localIsoWithOffset(now) };
    if (!this.isConnected()) {
      this.detail = "ccusage 未安装";
      return { ...base, error: this.detail };
    }
    try {
      const from = localDateKey(weekWindow(now).start);
      const to = localDateKey(now);
      const stdout = await this.run(["daily", "--since", from, "--until", to,
        "--by-agent", "--json", "--offline", "--no-cost", "--timezone",
        Intl.DateTimeFormat().resolvedOptions().timeZone]);
      const usage = parseLocalUsage(JSON.parse(stdout) as unknown, now);
      if (!usage) throw new Error("ccusage 未返回有效的本周用量");
      this.lastOkAt = now;
      this.detail = usage.agents.length ? `已发现 ${usage.agents.join(", ")}` : "已采集本地记录";
      return { ...base, ok: true, dailyTokens: usage.dailyTokens, agentUsage: usage.agentUsage, quotas: {
        weeklyTokens: { used: usage.weeklyTokens, cap: null, unit: "tokens",
          resetAt: localIsoWithOffset(weekWindow(now).end), basis: "ccusage-local" },
      } };
    } catch (err) {
      this.detail = err instanceof Error ? err.message : String(err);
      return { ...base, error: this.detail };
    }
  }

  status(): CollectorStatus {
    return { provider: this.provider, connected: this.isConnected(),
      lastRunAt: this.lastRunAt ? localIsoWithOffset(this.lastRunAt) : null,
      lastOkAt: this.lastOkAt ? localIsoWithOffset(this.lastOkAt) : null,
      detail: this.detail };
  }
}

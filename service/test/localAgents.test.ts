import { test } from "node:test";
import assert from "node:assert/strict";
import { LocalAgentsCollector, parseLocalUsage } from "../src/collectors/localAgents.js";

const now = new Date(2026, 8, 25, 12, 0);
const report = { daily: [
  { period: "2026-09-24", totalTokens: 120, agents: [{ agent: "claude" }] },
  { period: "2026-09-25", totalTokens: 350, agents: [{ agent: "codex" }, { agent: "opencode" }] },
] };

test("local agents: unified daily rows give today's and this week's totals", () => {
  assert.deepEqual(parseLocalUsage(report, now), {
    dailyTokens: 350, weeklyTokens: 470, agents: ["claude", "codex", "opencode"],
  });
  assert.equal(parseLocalUsage({ daily: [report.daily[0]] }, now)?.dailyTokens, 0);
  assert.equal(parseLocalUsage({ daily: [] }, now), null);
  assert.equal(parseLocalUsage({ daily: [{ period: "2026-09-25", totalTokens: -1 }] }, now), null);
});

test("local agents: collector invokes ccusage offline and reports valid usage", async () => {
  let args: string[] = [];
  const collector = new LocalAgentsCollector(async (input) => {
    args = input;
    return JSON.stringify(report);
  });
  const result = await collector.collect(now);
  assert.equal(result.ok, true);
  assert.equal(result.dailyTokens, 350);
  assert.equal(result.quotas?.weeklyTokens?.used, 470);
  assert.deepEqual(args.slice(0, 5), ["daily", "--since", "2026-09-21", "--until", "2026-09-25"]);
  assert.ok(args.includes("--offline"));
  assert.match(collector.status().detail, /codex/);
});

test("local agents: invalid ccusage output fails without a fabricated zero", async () => {
  const collector = new LocalAgentsCollector(async () => '{"daily":[]}');
  const result = await collector.collect(now);
  assert.equal(result.ok, false);
  assert.equal(result.dailyTokens, undefined);
});

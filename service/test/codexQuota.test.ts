import { test } from "node:test";
import assert from "node:assert/strict";
import { CodexQuotaCollector, parseCodexQuota } from "../src/collectors/codexQuota.js";
const now = new Date("2026-09-26T02:00:00Z");
const event = (timestamp = "2026-09-26T01:55:00Z", primary: unknown = {
  used_percent: 42, window_minutes: 10080, resets_at: now.getTime() / 1000 + 86400,
}, secondary: unknown = null) => JSON.stringify({ type: "event_msg", timestamp,
  payload: { type: "token_count", rate_limits: { limit_id: "codex", primary, secondary } } });

test("Codex quota: use window duration, never assume primary means five hours", () => {
  const sample = parseCodexQuota(event(undefined, undefined, {
    used_percent: 7, window_minutes: 299, resets_at: now.getTime() / 1000 + 3600,
  }), now)!;
  assert.equal(sample.quotas.weekly?.used, 42);
  assert.equal(sample.quotas.rolling5h?.used, 7);
  assert.equal(sample.quotas.weekly?.unit, "%");
});

test("Codex quota: rejects stale, future, expired, malformed, and non-Codex samples", () => {
  assert.equal(parseCodexQuota(event("2026-09-26T00:00:00Z"), now), null);
  assert.equal(parseCodexQuota(event("2026-09-26T03:00:00Z"), now), null);
  assert.equal(parseCodexQuota("partial\n{}\n", now), null);
  for (const used of [-1, 101, "42"]) {
    assert.deepEqual(parseCodexQuota(event(undefined, { used_percent: used,
      window_minutes: 300, resets_at: now.getTime() / 1000 + 5 }), now)?.quotas, {});
  }
  assert.deepEqual(parseCodexQuota(event(undefined, { used_percent: 1,
    window_minutes: 300, resets_at: now.getTime() / 1000 - 1 }), now)?.quotas, {});
  assert.equal(parseCodexQuota(event().replace('"codex"', '"other"'), now), null);
});

test("Codex quota: newest observation supersedes previous readings across files", async () => {
  const collector = new CodexQuotaCollector("/tmp", async () => [event(), event("2026-09-26T01:59:00Z", null)]);
  assert.equal((await collector.collect(now)).ok, false);
  const good = new CodexQuotaCollector("/tmp", async () => [event()]);
  const result = await good.collect(now);
  assert.equal(result.ok, true);
  assert.equal(result.collectedAt, "2026-09-26T01:55:00.000Z");
  assert.equal(result.dailyTokens, undefined);
});

import { test } from "node:test";
import assert from "node:assert/strict";
import {
  activityBlocks,
  aggregateClaudeUsage,
  hoursInWindow,
  parseUsageEntry,
} from "../src/collectors/claudeUsage.js";

const MIN = 60_000;
const HOUR = 3_600_000;

const validLine = (ts: string, input = 10, output = 20) =>
  JSON.stringify({
    type: "assistant",
    timestamp: ts,
    message: { id: "msg_1", model: "claude-x", usage: { input_tokens: input, output_tokens: output, cache_read_input_tokens: 999, cache_creation_input_tokens: 888 } },
  });

test("claude: parseUsageEntry accepts entries with timestamp + usage", () => {
  const e = parseUsageEntry(validLine("2026-09-21T02:00:00.000Z"));
  assert.ok(e);
  assert.equal(e.ts, Date.parse("2026-09-21T02:00:00.000Z"));
  assert.equal(e.inputTokens, 10);
  assert.equal(e.outputTokens, 20); // cache tokens excluded (ccusage default)
});

test("claude: parseUsageEntry rejects junk", () => {
  assert.equal(parseUsageEntry(""), null);
  assert.equal(parseUsageEntry("not json"), null);
  assert.equal(parseUsageEntry('{"timestamp":"2026-09-21T02:00:00Z"}'), null); // no usage
  assert.equal(parseUsageEntry('{"message":{"usage":{"input_tokens":1,"output_tokens":1}}}'), null); // no timestamp
  assert.equal(parseUsageEntry('{"timestamp":"bogus","message":{"usage":{"input_tokens":1,"output_tokens":1}}}'), null);
  assert.equal(parseUsageEntry(validLine("2026-09-21T02:00:00Z", 0, 0)), null); // zero usage
});

test("claude: activity blocks split on gaps > 5 minutes, merge otherwise", () => {
  const t0 = Date.parse("2026-09-21T02:00:00Z");
  const ts = [
    t0,
    t0 + 4 * MIN, // within gap -> same block
    t0 + 5 * MIN, // exactly gap boundary -> same block (<=)
    t0 + 11 * MIN, // > 5min from previous -> new block
    t0 + 60 * MIN, // new block
  ];
  const blocks = activityBlocks(ts);
  assert.equal(blocks.length, 3);
  assert.equal(blocks[0]!.start, t0);
  assert.equal(blocks[0]!.end, t0 + 5 * MIN);
  assert.equal(blocks[1]!.start, t0 + 11 * MIN);
  assert.equal(blocks[2]!.end, t0 + 60 * MIN);
  assert.deepEqual(activityBlocks([]), []);
  // single timestamp -> zero-duration block
  assert.deepEqual(activityBlocks([t0]), [{ start: t0, end: t0 }]);
});

test("claude: hoursInWindow clips blocks to the window", () => {
  const t0 = Date.parse("2026-09-21T00:00:00Z");
  const blocks = [
    { start: t0, end: t0 + 2 * HOUR }, // fully inside
    { start: t0 + 9 * HOUR, end: t0 + 12 * HOUR }, // straddles window end at 10h
  ];
  // window [0, 10h] -> 2h + 1h = 3h
  assert.equal(hoursInWindow(blocks, t0, t0 + 10 * HOUR), 3);
  // window [1h, 2h] fully inside first block -> 1h
  assert.equal(hoursInWindow(blocks, t0 + HOUR, t0 + 2 * HOUR), 1);
  // disjoint window -> 0
  assert.equal(hoursInWindow(blocks, t0 + 5 * HOUR, t0 + 6 * HOUR), 0);
});

test("claude: aggregate computes weekly hours, rolling 5h and weekly tokens", () => {
  // "now" is Wed 2026-09-23 12:00 +08:00 (fixed instant)
  const now = new Date("2026-09-23T04:00:00.000Z");

  const t = (h: number, m = 0) => now.getTime() - h * HOUR - m * MIN;
  // blocks must chain with gaps <= 5 min; a chain of messages 4 min apart
  // from t(20)..t(20)+4 and t(2)..t(2)+32 (9 messages).
  const entries = [
    { ts: t(70), inputTokens: 1000, outputTokens: 1000 }, // last week -> outside ISO week
    { ts: t(20), inputTokens: 100, outputTokens: 100 },
    { ts: t(20) + 4 * MIN, inputTokens: 100, outputTokens: 100 },
    { ts: t(2), inputTokens: 250_000, outputTokens: 150_000 },
    ...Array.from({ length: 8 }, (_, k) => ({
      ts: t(2) + (k + 1) * 4 * MIN,
      inputTokens: 5,
      outputTokens: 5,
    })),
  ];
  const stats = aggregateClaudeUsage(entries, now, { weeklyCapHours: 140, rolling5hCapHours: 36 });

  // block A: 4 min ; block B: 32 min -> weekly 36 min = 0.6 h
  assert.equal(stats.weeklyHours, 0.6);
  assert.equal(stats.blockCount, 3); // A, B, last-week single (zero duration)

  // weekly tokens: this week only: A=400, B=400000+8*10=400080 -> 400480
  assert.equal(stats.weeklyTokens, 400_480);

  // rolling 5h window opens at block B start; used = 32 min = 0.5 h (rounded)
  assert.equal(stats.rolling5hHours, 0.5);
  assert.ok(stats.rolling5hResetAt);
  assert.equal(stats.rolling5hResetAt!.getTime(), t(2) + 5 * HOUR);

  // weekly reset = next Monday 00:00 local
  assert.equal(stats.weeklyResetAt.getDay(), 1);
  assert.equal(stats.weeklyResetAt.getHours(), 0);
});

test("claude: no recent activity -> rolling 5h zero with null resetAt", () => {
  const now = new Date("2026-09-23T04:00:00.000Z");
  const entries = [
    { ts: now.getTime() - 10 * HOUR, inputTokens: 5, outputTokens: 5 },
    { ts: now.getTime() - 10 * HOUR + 3 * MIN, inputTokens: 5, outputTokens: 5 },
  ];
  const stats = aggregateClaudeUsage(entries, now, { weeklyCapHours: 140, rolling5hCapHours: 36 });
  assert.equal(stats.rolling5hHours, 0);
  assert.equal(stats.rolling5hResetAt, null);
  assert.equal(stats.weeklyHours, 0.1); // 3-minute burst this week
});

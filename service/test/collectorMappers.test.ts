import { test } from "node:test";
import assert from "node:assert/strict";
import { GlmCollector, mapGlmResponse } from "../src/collectors/glm.js";
import { DeepSeekCollector, mapDeepSeekBalance } from "../src/collectors/deepseek.js";

test("glm mapper: hours pair from coding-plan style responses", () => {
  assert.deepEqual(mapGlmResponse({ data: { usageHours: 5.5, totalHours: 120 } }), {
    kind: "hours",
    used: 5.5,
    total: 120,
    remaining: null,
  });
  assert.deepEqual(mapGlmResponse({ data: { used: 3, total: 100, extra: "x" } }), {
    kind: "unknown",
    used: 3,
    total: 100,
    remaining: null,
  });
});

test("glm mapper: prompts pair and remaining balance", () => {
  assert.deepEqual(mapGlmResponse({ data: { usedPrompts: 120, totalPrompts: 500 } }), {
    kind: "prompts",
    used: 120,
    total: 500,
    remaining: null,
  });
  assert.deepEqual(mapGlmResponse({ data: { remaining: 42.5 } }), {
    kind: "remaining",
    used: null,
    total: null,
    remaining: 42.5,
  });
});

test("glm mapper: nested shapes, cycles, and unrecognized input", () => {
  assert.deepEqual(
    mapGlmResponse({ code: 200, data: { plan: { usage_hours: 2, total_hours: 36 } } }),
    { kind: "hours", used: 2, total: 36, remaining: null },
  );
  const cyclic: Record<string, unknown> = { data: { remaining: 1 } };
  cyclic.self = cyclic;
  assert.deepEqual(mapGlmResponse(cyclic), { kind: "remaining", used: null, total: null, remaining: 1 });
  assert.equal(mapGlmResponse({ foo: "bar" }), null);
  assert.equal(mapGlmResponse(null), null);
  assert.equal(mapGlmResponse("ok"), null);
  // strings are numbers-only fields -> no fabrication from text
  assert.equal(mapGlmResponse({ data: { usedHours: "5", totalHours: "120" } }), null);
});

test("deepseek mapper: official balance payload", () => {
  const json = {
    is_available: true,
    balance_infos: [
      { currency: "CNY", total_balance: "110.50", granted_balance: "10.50", topped_up_balance: "100.00" },
    ],
  };
  assert.deepEqual(mapDeepSeekBalance(json), {
    available: true,
    currency: "CNY",
    remaining: 110.5,
    granted: 10.5,
    toppedUp: 100,
  });
});

test("deepseek mapper: picks CNY when multiple currencies, tolerates numbers", () => {
  const json = {
    is_available: false,
    balance_infos: [
      { currency: "USD", total_balance: "5.00" },
      { currency: "CNY", total_balance: 88.8, granted_balance: "0", topped_up_balance: "88.8" },
    ],
  };
  const mapped = mapDeepSeekBalance(json);
  assert.equal(mapped?.currency, "CNY");
  assert.equal(mapped?.remaining, 88.8);
  assert.equal(mapped?.available, false);
});

test("deepseek mapper: rejects malformed payloads", () => {
  assert.equal(mapDeepSeekBalance({}), null);
  assert.equal(mapDeepSeekBalance({ balance_infos: [] }), null);
  assert.equal(
    mapDeepSeekBalance({ balance_infos: [{ currency: "CNY", total_balance: "abc" }] }),
    null,
  );
  assert.equal(mapDeepSeekBalance(null), null);
  assert.equal(mapDeepSeekBalance({ balance_infos: [{ total_balance: "5" }] }), null);
  assert.equal(mapDeepSeekBalance({ balance_infos: [{ currency: "CNY", total_balance: "" }] }), null);
});

test("DeepSeek collector retains actual currency and never emits a Token quota", async t => {
  t.mock.method(globalThis, "fetch", async () => new Response(JSON.stringify({ is_available: true,
    balance_infos: [{ currency: "USD", total_balance: "5.25" }] })));
  const result = await new DeepSeekCollector({ getKey: () => "fixture" }).collect(new Date());
  assert.equal(result.ok, true);
  assert.deepEqual(result.balance, { remaining: 5.25, currency: "USD", basis: "api:deepseek-balance" });
  assert.equal(result.quotas, undefined);
});

test("GLM collector requires explicit unit/window and keeps request counts as requests", async t => {
  let payload: unknown = { data: { usedPrompts: 10, totalPrompts: 100 } };
  t.mock.method(globalThis, "fetch", async () => new Response(JSON.stringify(payload)));
  const collector = new GlmCollector({ getKey: () => "fixture" });
  assert.equal((await collector.collect(new Date())).ok, false);
  assert.equal(collector.status().lastOkAt, null);
  payload = { data: { usedPrompts: 10, totalPrompts: 100, window_minutes: 10080 } };
  const result = await collector.collect(new Date());
  assert.equal(result.ok, true);
  assert.equal(result.quotas?.weekly?.unit, "requests");
  assert.equal(result.quotas?.rolling5h, undefined);
});

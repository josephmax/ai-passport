import { test } from "node:test";
import assert from "node:assert/strict";
import { FailureLimiter } from "../src/util/rateLimit.js";

test("rate limit: blocks after max failures inside the window, forgets outside it", () => {
  const limiter = new FailureLimiter(3, 1000);
  const t0 = 1_000_000;
  assert.equal(limiter.isBlocked("a", new Date(t0)), false);
  limiter.recordFailure("a", new Date(t0));
  limiter.recordFailure("a", new Date(t0 + 500));
  assert.equal(limiter.isBlocked("a", new Date(t0 + 900)), false);
  limiter.recordFailure("a", new Date(t0 + 900));
  assert.equal(limiter.isBlocked("a", new Date(t0 + 950)), true);
  // The window slides: the first failure has expired.
  assert.equal(limiter.isBlocked("a", new Date(t0 + 1001)), false);
});

test("rate limit: reset clears one key without touching others", () => {
  const limiter = new FailureLimiter(1, 60_000);
  const now = new Date();
  limiter.recordFailure("a", now);
  limiter.recordFailure("b", now);
  assert.equal(limiter.isBlocked("a", now), true);
  limiter.reset("a");
  assert.equal(limiter.isBlocked("a", now), false);
  assert.equal(limiter.isBlocked("b", now), true);
});

test("rate limit: unknown keys are never blocked", () => {
  const limiter = new FailureLimiter(1, 60_000);
  limiter.recordFailure("a", new Date());
  assert.equal(limiter.isBlocked("never-seen", new Date()), false);
});

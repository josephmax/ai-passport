import { test } from "node:test";
import assert from "node:assert/strict";
import {
  MAX_LEVEL,
  XP_SEGMENTS,
  costToNextLevel,
  levelForXp,
  totalXpToMaxLevel,
  xpToNextLevel,
} from "../src/xpTable.js";

test("xp: segment table mirrors spec §6.2", () => {
  assert.deepEqual(XP_SEGMENTS, [
    { fromLevel: 1, toLevel: 9, costToNext: 5 },
    { fromLevel: 10, toLevel: 29, costToNext: 15 },
    { fromLevel: 30, toLevel: 59, costToNext: 25 },
    { fromLevel: 60, toLevel: 98, costToNext: 50 },
    { fromLevel: 99, toLevel: 99, costToNext: 100 },
  ]);
});

test("xp: full level 100 costs exactly 3145 tomatoes", () => {
  assert.equal(totalXpToMaxLevel(), 3145);
  // 9*5 + 20*15 + 30*25 + 39*50 + 1*100
  assert.equal(9 * 5 + 20 * 15 + 30 * 25 + 39 * 50 + 100, 3145);
});

test("xp: costToNextLevel boundaries", () => {
  assert.equal(costToNextLevel(1), 5);
  assert.equal(costToNextLevel(9), 5);
  assert.equal(costToNextLevel(10), 15);
  assert.equal(costToNextLevel(29), 15);
  assert.equal(costToNextLevel(30), 25);
  assert.equal(costToNextLevel(59), 25);
  assert.equal(costToNextLevel(60), 50);
  assert.equal(costToNextLevel(98), 50);
  assert.equal(costToNextLevel(99), 100);
  assert.equal(costToNextLevel(100), null);
  assert.equal(costToNextLevel(0), null);
});

test("xp: levelForXp reverse lookup", () => {
  assert.equal(levelForXp(0), 1);
  assert.equal(levelForXp(-3), 1);
  assert.equal(levelForXp(4), 1);
  assert.equal(levelForXp(5), 2);
  assert.equal(levelForXp(44), 9);
  assert.equal(levelForXp(45), 10); // 9*5 = 45 -> level 10
  assert.equal(levelForXp(3144), 99);
  assert.equal(levelForXp(3145), MAX_LEVEL);
  assert.equal(levelForXp(10_000), MAX_LEVEL);
});

test("xp: levelForXp is monotonic across the whole range", () => {
  let prev = 1;
  for (let xp = 0; xp <= 3200; xp++) {
    const lvl = levelForXp(xp);
    assert.ok(lvl >= prev, `monotonic at xp=${xp}`);
    assert.ok(lvl <= MAX_LEVEL);
    prev = lvl;
  }
});

test("xp: xpToNextLevel", () => {
  assert.equal(xpToNextLevel(0), 5);
  assert.equal(xpToNextLevel(4), 1);
  assert.equal(xpToNextLevel(5), 5); // level 2, still in 1-9 segment
  assert.equal(xpToNextLevel(45), 15); // just reached level 10
  assert.equal(xpToNextLevel(3145), 0); // at max level
  assert.equal(xpToNextLevel(3144), 1); // one short of max
  assert.equal(xpToNextLevel(999_999), 0);
});

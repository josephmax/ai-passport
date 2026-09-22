import { test } from "node:test";
import assert from "node:assert/strict";
import { wmoKind, type WeatherKind } from "../src/wmo.js";

/**
 * Mirror check against the firmware rule (main/app/app_weather.c):
 * 0-2 sunny; 3/45/48 cloudy; 51-67 & 80-82 rain; 71-77 & 85-86 snow; else cloudy.
 */
function firmwareRule(code: number): WeatherKind {
  if (code >= 0 && code <= 2) return "sunny";
  if (code === 3 || code === 45 || code === 48) return "cloudy";
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return "rain";
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return "snow";
  return "cloudy";
}

test("wmo: service mirror matches firmware rule for all 0..99 codes", () => {
  for (let code = 0; code <= 99; code++) {
    assert.equal(wmoKind(code), firmwareRule(code), `code ${code}`);
  }
});

test("wmo: spot values", () => {
  assert.equal(wmoKind(0), "sunny");
  assert.equal(wmoKind(1), "sunny");
  assert.equal(wmoKind(2), "sunny");
  assert.equal(wmoKind(3), "cloudy");
  assert.equal(wmoKind(45), "cloudy");
  assert.equal(wmoKind(48), "cloudy");
  assert.equal(wmoKind(51), "rain");
  assert.equal(wmoKind(61), "rain"); // spec example: rain
  assert.equal(wmoKind(67), "rain");
  assert.equal(wmoKind(80), "rain");
  assert.equal(wmoKind(82), "rain");
  assert.equal(wmoKind(71), "snow");
  assert.equal(wmoKind(77), "snow");
  assert.equal(wmoKind(85), "snow");
  assert.equal(wmoKind(86), "snow");
});

test("wmo: unknown/unassigned codes degrade to cloudy", () => {
  for (const code of [4, 5, 10, 17, 19, 49, 50, 68, 69, 70, 78, 79, 83, 84, 87, 90, 95, 96, 99, -1, -50, 100, 200]) {
    assert.equal(wmoKind(code), "cloudy", `code ${code}`);
  }
});

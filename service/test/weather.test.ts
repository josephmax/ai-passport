import { test } from "node:test";
import assert from "node:assert/strict";
import { buildForecastUrl, mapOpenMeteoResponse } from "../src/weather.js";
import { wmoKind } from "../src/wmo.js";

const sample = {
  latitude: 31.23,
  longitude: 121.47,
  timezone: "Asia/Shanghai",
  current: { time: "2026-09-22T14:00", temperature_2m: 26.1, weather_code: 61 },
  daily: {
    time: ["2026-09-22"],
    sunrise: ["2026-09-22T06:12"],
    sunset: ["2026-09-22T18:05"],
  },
};

test("weather: open-meteo response maps to the snapshot fields", () => {
  const w = mapOpenMeteoResponse(sample, "上海", "2026-09-22T14:00:00+08:00");
  assert.ok(w);
  assert.equal(w!.code, 61);
  assert.equal(w!.kind, "rain"); // WMO mirror applied
  assert.equal(w!.sunrise, "06:12");
  assert.equal(w!.sunset, "18:05");
  assert.equal(w!.city, "上海");
});

test("weather: missing sunrise/sunset falls back to neutral defaults, bad input -> null", () => {
  const noDaily = mapOpenMeteoResponse(
    { current: { weather_code: 0 } },
    "x",
    "t",
  );
  assert.equal(noDaily!.code, 0);
  assert.equal(noDaily!.kind, "sunny");
  assert.equal(noDaily!.sunrise, "06:00");
  assert.equal(noDaily!.sunset, "18:00");

  assert.equal(mapOpenMeteoResponse({ daily: sample.daily }, "x", "t"), null);
  assert.equal(mapOpenMeteoResponse({ current: {} }, "x", "t"), null);
  assert.equal(mapOpenMeteoResponse(null, "x", "t"), null);
});

test("weather: forecast URL carries coords and key-less params", () => {
  const url = new URL(buildForecastUrl(31.2304, 121.4737));
  assert.equal(url.hostname, "api.open-meteo.com");
  assert.equal(url.pathname, "/v1/forecast");
  assert.equal(url.searchParams.get("current"), "weather_code");
  assert.equal(url.searchParams.get("daily"), "sunrise,sunset");
  assert.equal(url.searchParams.get("timezone"), "auto");
  assert.equal(url.searchParams.get("latitude"), "31.2304");
});

test("weather: WMO integration spot check across kinds", () => {
  for (const [code, kind] of [[0, "sunny"], [2, "sunny"], [3, "cloudy"], [61, "rain"], [71, "snow"], [99, "cloudy"]] as const) {
    const w = mapOpenMeteoResponse({ current: { weather_code: code } }, "c", "t");
    assert.equal(w!.kind, kind);
    assert.equal(w!.kind, wmoKind(code));
  }
});

import { test } from "node:test";
import assert from "node:assert/strict";
import { CITY_TABLE, findCity, resolveLocation } from "../src/cityTable.js";
import { localIsoWithOffset, weekWindow } from "../src/util/time.js";

test("city table: ~30 cities, unique ids, valid coordinates", () => {
  assert.ok(CITY_TABLE.length >= 30, `expected >=30 cities, got ${CITY_TABLE.length}`);
  const ids = new Set(CITY_TABLE.map((c) => c.id));
  assert.equal(ids.size, CITY_TABLE.length);
  for (const c of CITY_TABLE) {
    assert.ok(c.lat >= -90 && c.lat <= 90, `${c.id} lat`);
    assert.ok(c.lon >= -180 && c.lon <= 180, `${c.id} lon`);
    assert.ok(c.name.length > 0);
  }
  // spec example city present
  assert.equal(findCity("shanghai")?.name, "上海");
  assert.equal(findCity("beijing")?.name, "北京");
  assert.equal(findCity("nope"), undefined);
});

test("city table: resolveLocation prefers custom lat/lon, falls back to city then default", () => {
  const custom = resolveLocation("beijing", 39.9, 116.4);
  assert.deepEqual([custom.lat, custom.lon, custom.name], [39.9, 116.4, "自定义"]);

  const city = resolveLocation("hangzhou", null, null);
  assert.deepEqual([city.lat, city.lon, city.name], [30.2741, 120.1551, "杭州"]);

  const fallback = resolveLocation("bogus", null, null);
  assert.equal(fallback.name, CITY_TABLE[0]!.name);
});

test("time: localIsoWithOffset round-trips the instant at second precision", () => {
  const d = new Date();
  const s = localIsoWithOffset(d);
  assert.match(s, /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|[+-]\d{2}:\d{2})$/);
  // sub-second precision is intentionally dropped (spec format has seconds)
  const floorToSecond = Math.floor(d.getTime() / 1000) * 1000;
  assert.equal(Date.parse(s), floorToSecond);
  const fixed = localIsoWithOffset(new Date("2026-09-22T06:00:00"));
  assert.equal(Date.parse(fixed), new Date(2026, 8, 22, 6, 0, 0).getTime());
});

test("time: weekWindow is Monday 00:00 -> next Monday 00:00 local", () => {
  // Wednesday 2026-09-23
  const wed = new Date(2026, 8, 23, 15, 30);
  const w = weekWindow(wed);
  assert.equal(w.start.getDay(), 1);
  assert.equal(w.start.getDate(), 21); // Monday Sep 21
  assert.equal(w.start.getMonth(), 8);
  assert.equal(w.start.getHours(), 0);
  assert.equal(w.end.getDate(), 28);
  assert.equal(w.end.getTime() - w.start.getTime(), 7 * 24 * 3_600_000);

  // Monday midnight belongs to the week starting that Monday
  const mon = new Date(2026, 8, 21, 0, 0);
  assert.equal(weekWindow(mon).start.getDate(), 21);

  // Sunday rolls back to the previous Monday
  const sun = new Date(2026, 8, 27, 23, 59);
  assert.equal(weekWindow(sun).start.getDate(), 21);
});

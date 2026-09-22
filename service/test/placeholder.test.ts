import { test } from "node:test";
import assert from "node:assert/strict";
import {
  ACTION_SPECS,
  generatePlaceholderBundle,
  generatePlaceholderDataFiles,
  placeholderManifest,
} from "../src/assets/placeholder.js";
import { MANIFEST_NAME, parseBundle, BUNDLE_MAX_BYTES } from "../src/assets/bundleFormat.js";

const FRAME_BYTES = (w: number, h: number, frames: number) => w * h * 2 * frames;

test("placeholder: manifest matches the spec example field-by-field", () => {
  const m = placeholderManifest(1);
  assert.deepEqual(m.actions.run, { file: "run.bin", frames: 6, fps: 6, w: 64, h: 64 });
  assert.deepEqual(m.actions.fight, { file: "fight.bin", frames: 6, fps: 8, w: 64, h: 64 });
  assert.deepEqual(m.actions.sleep, { file: "sleep.bin", frames: 2, fps: 2, w: 64, h: 64 });
  assert.deepEqual(m.actions.victory, { file: "victory.bin", frames: 4, fps: 6, w: 64, h: 64 });
  assert.deepEqual(m.map, { file: "map.bin", w: 240, h: 160 });
  assert.deepEqual(m.decorations, [{ file: "deco0.bin", w: 24, h: 24 }]);
  assert.deepEqual(m.weather.rain, { file: "rain.bin", frames: 2, fps: 6, w: 16, h: 16 });
  assert.deepEqual(m.weather.snow, { file: "snow.bin", frames: 2, fps: 4, w: 16, h: 16 });
  assert.equal(m.version, 1);
});

test("placeholder: frame files have exact RGB565 sizes (w*h*2 per frame)", () => {
  const files = generatePlaceholderDataFiles();
  assert.equal(files.get("run.bin")!.length, FRAME_BYTES(64, 64, ACTION_SPECS.run.frames));
  assert.equal(files.get("fight.bin")!.length, FRAME_BYTES(64, 64, ACTION_SPECS.fight.frames));
  assert.equal(files.get("sleep.bin")!.length, FRAME_BYTES(64, 64, ACTION_SPECS.sleep.frames));
  assert.equal(files.get("victory.bin")!.length, FRAME_BYTES(64, 64, ACTION_SPECS.victory.frames));
  assert.equal(files.get("map.bin")!.length, FRAME_BYTES(240, 160, 1));
  assert.equal(files.get("deco0.bin")!.length, FRAME_BYTES(24, 24, 1));
  assert.equal(files.get("rain.bin")!.length, FRAME_BYTES(16, 16, 2));
  assert.equal(files.get("snow.bin")!.length, FRAME_BYTES(16, 16, 2));
});

test("placeholder: bundles are valid APB1, deterministic, under the 4 MB cap", () => {
  const a = generatePlaceholderBundle(1);
  const b = generatePlaceholderBundle(1);
  assert.deepEqual(a, b); // deterministic pure generation
  assert.ok(a.length > 1000); // non-trivial art
  assert.ok(a.length <= BUNDLE_MAX_BYTES);

  const parsed = parseBundle(a);
  assert.equal(parsed.version, 1);
  const names = parsed.files.map((f) => f.name).sort();
  assert.deepEqual(names, [
    "deco0.bin", "fight.bin", "manifest.json", "map.bin", "rain.bin",
    "run.bin", "sleep.bin", "snow.bin", "victory.bin",
  ]);
  const manifest = JSON.parse(parsed.files.find((f) => f.name === MANIFEST_NAME)!.data.toString("utf8"));
  assert.equal(manifest.version, 1);
  // every manifest-referenced file exists in the bundle
  const present = new Set(parsed.files.map((f) => f.name));
  for (const key of ["run", "fight", "sleep", "victory"] as const) {
    assert.ok(present.has(manifest.actions[key].file), key);
  }
  assert.ok(present.has(manifest.map.file));
  assert.ok(present.has(manifest.decorations[0].file));
  assert.ok(present.has(manifest.weather.rain.file));
  assert.ok(present.has(manifest.weather.snow.file));
});

test("placeholder: actions are non-blank (contain non-zero pixels)", () => {
  const files = generatePlaceholderDataFiles();
  for (const [name, data] of files) {
    const nonZero = data.some((b) => b !== 0);
    assert.ok(nonZero, `${name} is all black`);
  }
});

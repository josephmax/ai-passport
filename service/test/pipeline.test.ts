import { test } from "node:test";
import assert from "node:assert/strict";
import sharp from "sharp";
import {
  ACTION_RULES,
  AssetValidationError,
  convertPngToRgb565,
  convertWithSize,
  validateActionFrameCount,
  validateFps,
} from "../src/assets/pipeline.js";

async function pngFromRaw(w: number, h: number, fill: [number, number, number]): Promise<Buffer> {
  const data = Buffer.alloc(w * h * 3);
  for (let i = 0; i < w * h; i++) {
    data[i * 3] = fill[0];
    data[i * 3 + 1] = fill[1];
    data[i * 3 + 2] = fill[2];
  }
  return sharp(data, { raw: { width: w, height: h, channels: 3 } }).png().toBuffer();
}

test("pipeline: PNG -> RGB565 LE frame via sharp", async () => {
  const png = await pngFromRaw(64, 64, [255, 0, 0]);
  const img = await convertPngToRgb565(png);
  assert.equal(img.w, 64);
  assert.equal(img.h, 64);
  assert.equal(img.data.length, 64 * 64 * 2);
  // first pixel red -> 00 F8 (little-endian)
  assert.equal(img.data[0], 0x00);
  assert.equal(img.data[1], 0xf8);
  // all pixels identical
  assert.ok(img.data.every((b) => b === 0x00 || b === 0xf8));
});

test("pipeline: accepts PNG with alpha (alpha discarded)", async () => {
  const rgba = Buffer.alloc(8 * 8 * 4);
  for (let i = 0; i < 8 * 8; i++) {
    rgba[i * 4] = 0;
    rgba[i * 4 + 1] = 255;
    rgba[i * 4 + 2] = 0;
    rgba[i * 4 + 3] = 128;
  }
  const png = await sharp(rgba, { raw: { width: 8, height: 8, channels: 4 } }).png().toBuffer();
  const img = await convertPngToRgb565(png);
  assert.equal(img.data.length, 8 * 8 * 2);
  assert.deepEqual([...img.data.subarray(0, 2)], [0xe0, 0x07]);
});

test("pipeline: rejects non-PNG input and wrong dimensions", async () => {
  const jpeg = await sharp(Buffer.alloc(16 * 16 * 3, 128), {
    raw: { width: 16, height: 16, channels: 3 },
  })
    .jpeg()
    .toBuffer();
  await assert.rejects(() => convertPngToRgb565(jpeg), AssetValidationError);
  await assert.rejects(() => convertPngToRgb565(Buffer.from("not an image")), AssetValidationError);

  const png = await pngFromRaw(63, 64, [0, 0, 255]);
  await assert.rejects(() => convertWithSize(png, 64, 64, "run"), /64×64/);
});

test("pipeline: frame-count and fps rules mirror the spec", () => {
  const cases: [keyof typeof ACTION_RULES, number, boolean][] = [
    ["run", 3, false], ["run", 4, true], ["run", 8, true], ["run", 9, false],
    ["fight", 4, true], ["fight", 8, true], ["fight", 9, false],
    ["sleep", 1, false], ["sleep", 2, true], ["sleep", 4, true], ["sleep", 5, false],
    ["victory", 1, true], ["victory", 4, true], ["victory", 5, false],
  ];
  for (const [slot, frames, ok] of cases) {
    if (ok) {
      validateActionFrameCount(slot, frames);
    } else {
      assert.throws(() => validateActionFrameCount(slot, frames), AssetValidationError);
    }
  }

  assert.equal(validateFps(1), 1);
  assert.equal(validateFps(10), 10);
  assert.equal(validateFps(6.4), 6);
  assert.throws(() => validateFps(0), AssetValidationError);
  assert.throws(() => validateFps(11), AssetValidationError);
});

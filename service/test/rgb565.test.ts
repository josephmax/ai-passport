import { test } from "node:test";
import assert from "node:assert/strict";
import { Rgb565Canvas, rawToRgb565Le, rgbTo565 } from "../src/assets/rgb565.js";

test("rgb565: primary colors pack correctly", () => {
  assert.equal(rgbTo565(255, 0, 0).toString(16), "f800");
  assert.equal(rgbTo565(0, 255, 0).toString(16), "7e0");
  assert.equal(rgbTo565(0, 0, 255).toString(16), "1f");
  assert.equal(rgbTo565(255, 255, 255).toString(16), "ffff");
  assert.equal(rgbTo565(0, 0, 0).toString(16), "0");
  assert.equal(rgbTo565(3, 3, 3).toString(16), "0"); // below every quantization step
  assert.equal(rgbTo565(8, 8, 8).toString(16), "841"); // lowest nonzero step per channel
  assert.equal(rgbTo565(248, 252, 248).toString(16), "ffff");
});

test("rgb565: raw RGBA serializes little-endian u16 per pixel", () => {
  // 2x1 image: red, blue; RGBA (4 channels)
  const data = Buffer.from([
    255, 0, 0, 255, //
    0, 0, 255, 128,
  ]);
  const out = rawToRgb565Le({ width: 2, height: 1, data, channels: 4 });
  assert.equal(out.length, 4); // w*h*2
  // red = 0xF800 -> LE bytes 00 F8 ; blue = 0x001F -> LE bytes 1F 00
  assert.deepEqual([...out], [0x00, 0xf8, 0x1f, 0x00]);
});

test("rgb565: 3-channel RGB input gives identical output as 4-channel", () => {
  const rgb = Buffer.from([255, 0, 0, 0, 255, 0]);
  const rgba = Buffer.from([255, 0, 0, 9, 0, 255, 0, 9]);
  assert.deepEqual(
    rawToRgb565Le({ width: 2, height: 1, data: rgb, channels: 3 }),
    rawToRgb565Le({ width: 2, height: 1, data: rgba, channels: 4 }),
  );
});

test("rgb565: canvas draws and clips, frame size = w*h*2", () => {
  const c = new Rgb565Canvas(4, 3);
  c.set(0, 0, 255, 0, 0);
  c.set(99, 99, 255, 255, 255); // clipped, no throw
  c.fillRect(1, 1, 2, 1, 0, 255, 0); // pixels (1,1) and (2,1)
  const buf = c.toBuffer();
  assert.equal(buf.length, 4 * 3 * 2);
  assert.deepEqual([...buf.subarray(0, 2)], [0x00, 0xf8]); // (0,0) red
  assert.deepEqual([...buf.subarray(2, 4)], [0x00, 0x00]); // (1,0) untouched
  assert.deepEqual([...buf.subarray(10, 12)], [0xe0, 0x07]); // (1,1) green 0x07E0 LE
  assert.deepEqual([...buf.subarray(12, 14)], [0xe0, 0x07]); // (2,1) green
});

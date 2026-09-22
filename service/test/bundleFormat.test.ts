import { test } from "node:test";
import assert from "node:assert/strict";
import {
  BUNDLE_MAGIC,
  BundleFormatError,
  packBundle,
  parseBundle,
  type BundleFile,
} from "../src/assets/bundleFormat.js";

const files: BundleFile[] = [
  { name: "manifest.json", data: Buffer.from('{"version":7}', "utf8") },
  { name: "run.bin", data: Buffer.alloc(64 * 64 * 2 * 6, 0xab) },
  { name: "deco/装饰0.bin", data: Buffer.from([1, 2, 3]) },
];

test("bundle: header bytes are magic + u16 LE version + u16 LE count", () => {
  const buf = packBundle(7, files);
  assert.equal(buf.subarray(0, 4).toString("ascii"), "APB1");
  assert.deepEqual([...buf.subarray(0, 4)], [0x41, 0x50, 0x42, 0x31]);
  assert.equal(buf.readUInt16LE(4), 7);
  assert.equal(buf[4], 7); // little-endian: low byte first
  assert.equal(buf[5], 0);
  assert.equal(buf.readUInt16LE(6), 3);
});

test("bundle: entry layout is u16 name_len LE + name + u32 data_len LE + data", () => {
  const buf = packBundle(1, files);
  let off = 8;
  const nameLen = buf.readUInt16LE(off);
  const name = buf.subarray(off + 2, off + 2 + nameLen).toString("utf8");
  assert.equal(name, "manifest.json");
  off += 2 + nameLen;
  const dataLen = buf.readUInt32LE(off);
  assert.equal(dataLen, files[0]!.data.length);
  // u32 LE: for small lengths only the first byte is set
  assert.deepEqual([...buf.subarray(off, off + 4)], [dataLen, 0, 0, 0]);
  off += 4;
  assert.equal(buf.subarray(off, off + dataLen).toString("utf8"), '{"version":7}');
});

test("bundle: pack/parse roundtrip preserves files, names and version", () => {
  const buf = packBundle(42, files);
  const parsed = parseBundle(buf);
  assert.equal(parsed.version, 42);
  assert.equal(parsed.files.length, 3);
  assert.deepEqual(parsed.files[0]!.data, files[0]!.data);
  assert.equal(parsed.files[1]!.name, "run.bin");
  assert.equal(parsed.files[1]!.data.length, 64 * 64 * 2 * 6);
  assert.equal(parsed.files[2]!.name, "deco/装饰0.bin"); // UTF-8 relative path
  assert.deepEqual(parsed.files[2]!.data, files[2]!.data);
});

test("bundle: rejects bad magic, truncation, trailing bytes", () => {
  const buf = packBundle(1, files);
  const bad = Buffer.from(buf);
  bad[0] = 0x00;
  assert.throws(() => parseBundle(bad), BundleFormatError);
  assert.throws(() => parseBundle(buf.subarray(0, buf.length - 1)), BundleFormatError);
  assert.throws(() => parseBundle(Buffer.concat([buf, Buffer.from([0])])), BundleFormatError);
  assert.throws(() => parseBundle(Buffer.alloc(4)), BundleFormatError);
});

test("bundle: rejects unsafe file names and bad versions", () => {
  assert.throws(() => packBundle(1, [{ name: "/abs.bin", data: Buffer.alloc(1) }]), BundleFormatError);
  assert.throws(() => packBundle(1, [{ name: "../evil.bin", data: Buffer.alloc(1) }]), BundleFormatError);
  assert.throws(() => packBundle(1, [{ name: "a\\b.bin", data: Buffer.alloc(1) }]), BundleFormatError);
  assert.throws(() => packBundle(0, files), BundleFormatError);
  assert.throws(() => packBundle(70_000, files), BundleFormatError);
  assert.throws(() => packBundle(1, []), BundleFormatError);
});

test("bundle: enforces the 4 MB cap", () => {
  const big: BundleFile[] = [
    { name: "manifest.json", data: Buffer.from('{"version":1}') },
    { name: "big.bin", data: Buffer.alloc(4 * 1024 * 1024 + 1, 1) },
  ];
  assert.throws(() => packBundle(1, big), /4 MB/);
});

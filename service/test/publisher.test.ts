import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { AssetPublisher } from "../src/assets/publisher.js";
import { DraftStore } from "../src/assets/draftStore.js";
import { parseBundle, MANIFEST_NAME } from "../src/assets/bundleFormat.js";
import { generatePlaceholderDataFiles } from "../src/assets/placeholder.js";

function fresh(): { dir: string; cleanup: () => void } {
  const dir = mkdtempSync(path.join(tmpdir(), "pp-assets-"));
  return { dir, cleanup: () => rmSync(dir, { recursive: true, force: true }) };
}

test("publisher: ensurePlaceholderPublished seeds v1 exactly once", async (t) => {
  const { dir, cleanup } = fresh();
  t.after(cleanup);
  const pub = new AssetPublisher(dir);
  assert.equal(pub.currentVersion(), 0);
  const seeded = await pub.ensurePlaceholderPublished();
  assert.equal(seeded?.version, 1);
  assert.equal(pub.currentVersion(), 1);
  const again = await pub.ensurePlaceholderPublished();
  assert.equal(again, null); // never re-seeds
  const buf = await pub.readBundle(1);
  assert.ok(buf);
  const parsed = parseBundle(buf!);
  assert.equal(parsed.version, 1);
});

test("publisher: draft with partial slots falls back to placeholder, versions auto-increment", async (t) => {
  const { dir, cleanup } = fresh();
  t.after(cleanup);
  const pub = new AssetPublisher(dir);
  await pub.ensurePlaceholderPublished();

  const draftStore = new DraftStore(dir);
  const fakeFrames = [
    Buffer.alloc(64 * 64 * 2, 0x11),
    Buffer.alloc(64 * 64 * 2, 0x22),
  ];
  await draftStore.setAction("sleep", fakeFrames, 3, 64, 64);

  const entry = await pub.publishFromDraft(draftStore.get());
  assert.equal(entry.version, 2);
  assert.equal(pub.currentVersion(), 2);

  const parsed = parseBundle((await pub.readBundle(2))!);
  const manifest = JSON.parse(
    parsed.files.find((f) => f.name === MANIFEST_NAME)!.data.toString("utf8"),
  );
  assert.equal(manifest.version, 2);
  // custom sleep slot honored
  assert.equal(manifest.actions.sleep.frames, 2);
  assert.equal(manifest.actions.sleep.fps, 3);
  const sleep = parsed.files.find((f) => f.name === "sleep.bin")!;
  assert.equal(sleep.data.length, 64 * 64 * 2 * 2);
  assert.equal(sleep.data[0], 0x11);
  // untouched slot falls back to placeholder art
  const placeholder = generatePlaceholderDataFiles();
  assert.deepEqual(parsed.files.find((f) => f.name === "run.bin")!.data, placeholder.get("run.bin"));
  // weather always built-in
  assert.deepEqual(parsed.files.find((f) => f.name === "rain.bin")!.data, placeholder.get("rain.bin"));
});

test("publisher: rollback republishes old content under a new version", async (t) => {
  const { dir, cleanup } = fresh();
  t.after(cleanup);
  const pub = new AssetPublisher(dir);
  await pub.ensurePlaceholderPublished(); // v1

  const draftStore = new DraftStore(dir);
  await draftStore.setAction("sleep", [Buffer.alloc(64 * 64 * 2, 0x99)], 1, 64, 64);
  await pub.publishFromDraft(draftStore.get()); // v2

  const rolled = await pub.rollback(1);
  assert.equal(rolled.version, 3);
  assert.equal(pub.currentVersion(), 3);

  const v3 = parseBundle((await pub.readBundle(3))!);
  const placeholder = generatePlaceholderDataFiles();
  // v3 content == v1 content (placeholder sleep), only the version differs
  assert.deepEqual(v3.files.find((f) => f.name === "sleep.bin")!.data, placeholder.get("sleep.bin"));
  const manifest = JSON.parse(v3.files.find((f) => f.name === MANIFEST_NAME)!.data.toString("utf8"));
  assert.equal(manifest.version, 3);
  assert.equal(pub.history()[0]!.source, "rollback:v1");

  await assert.rejects(() => pub.rollback(99), /不存在/);
});

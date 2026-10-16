import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import test from "node:test";
import { prepareDesktop } from "../tools/desktop-runtime.mjs";

test("desktop state is private, persistent and independent of its bundle", () => {
  const root = mkdtempSync(path.join(tmpdir(), "passport desktop "));
  try {
    const initial = prepareDesktop(root);
    assert.equal(initial.HOST, "127.0.0.1");
    assert.equal(initial.PORT, "0");
    assert.equal(initial.DATA_DIR, path.join(root, "data"));
    assert.match(initial.PORTAL_PASSWORD, /^[0-9a-f]{36}$/);
    assert.match(initial.SESSION_SECRET, /^[0-9a-f]{64}$/);
    assert.deepEqual(prepareDesktop(root), initial);
    assert.equal(statSync(root).mode & 0o777, 0o700);
    assert.equal(statSync(path.join(root, ".env")).mode & 0o777, 0o600);
    writeFileSync(path.join(root, ".env"), readFileSync(path.join(root, ".env"), "utf8") + "HOST=0.0.0.0\nPORT=3000\nDATA_DIR=/other\n");
    assert.deepEqual(prepareDesktop(root), initial);
    writeFileSync(path.join(root, ".env"), "corrupt");
    assert.throws(() => prepareDesktop(root), /refusing to reset/);
    assert.equal(readFileSync(path.join(root, ".env"), "utf8"), "corrupt");
  } finally { rmSync(root, { recursive: true }); }
});

test("desktop data path must be explicit and absolute", () => {
  assert.throws(() => prepareDesktop("relative"), /absolute/);
  assert.throws(() => prepareDesktop(""), /absolute/);
});

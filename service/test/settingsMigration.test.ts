import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { SettingsStore } from "../src/store/settings.js";

test("legacy ChatGPT primary migrates to the actual Codex source", () => {
  const dir = mkdtempSync(path.join(tmpdir(), "passport-settings-"));
  try {
    writeFileSync(path.join(dir, "config.json"), JSON.stringify({ primaryAccount: "chatgpt" }));
    assert.equal(new SettingsStore(dir).get().primaryAccount, "codex");
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

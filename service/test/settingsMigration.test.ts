import { test } from "node:test";
import assert from "node:assert/strict";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { SettingsStore } from "../src/store/settings.js";

test("legacy provider ids migrate to current sources and persist", () => {
  for (const [legacy, migrated] of [["chatgpt", "codex"], ["claude", "local"]] as const) {
    const dir = mkdtempSync(path.join(tmpdir(), "passport-settings-"));
    try {
      const config = path.join(dir, "config.json");
      writeFileSync(config, JSON.stringify({ primaryAccount: legacy }));
      const store = new SettingsStore(dir);
      assert.equal(store.get().primaryAccount, migrated);
      // The migration is persisted, not just patched in memory.
      assert.ok(existsSync(config));
      assert.equal(JSON.parse(readFileSync(config, "utf8")).primaryAccount, migrated);
      // A reload is a no-op (idempotent).
      assert.equal(new SettingsStore(dir).get().primaryAccount, migrated);
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  }
});

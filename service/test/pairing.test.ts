import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { DeviceRegistry } from "../src/store/devices.js";

function freshRegistry(): { reg: DeviceRegistry; cleanup: () => void } {
  const dir = mkdtempSync(path.join(tmpdir(), "pp-devices-"));
  return { reg: new DeviceRegistry(dir), cleanup: () => rmSync(dir, { recursive: true, force: true }) };
}

test("pairing: codes are 6 digits and valid for 10 minutes, single use", async (t) => {
  const { reg, cleanup } = freshRegistry();
  t.after(cleanup);
  const t0 = new Date("2026-09-22T08:00:00Z");
  const { code } = reg.generatePairingCode(t0);
  assert.match(code, /^\d{6}$/);

  // success -> 32 hex token, device registered
  const out = await reg.pair(code, "My Pendant", t0);
  assert.ok(out.ok, out.reason);
  assert.match(out.token!, /^[0-9a-f]{32}$/);
  assert.equal(reg.list().length, 1);
  assert.equal(reg.list()[0]!.name, "My Pendant");
  assert.equal(reg.list()[0]!.status, "active");

  // single use: same code again -> rejected
  const again = await reg.pair(code, "Sneaky", t0);
  assert.equal(again.ok, false);
  assert.equal(again.reason, "used");
  assert.equal(reg.list().length, 1);
});

test("pairing: wrong code invalid, expiry after 10 minutes", async (t) => {
  const { reg, cleanup } = freshRegistry();
  t.after(cleanup);
  const t0 = new Date("2026-09-22T08:00:00Z");

  const wrong = await reg.pair("000000", "x", t0);
  assert.equal(wrong.ok, false);
  assert.equal(wrong.reason, "invalid");

  const { code } = reg.generatePairingCode(t0);
  const late = await reg.pair(code, "x", new Date(t0.getTime() + 10 * 60_000 + 1000));
  assert.equal(late.ok, false);
  assert.equal(late.reason, "expired");
  // boundary: 1 second before expiry is still valid (codes are valid FOR 10 minutes)
  const { code: c2 } = reg.generatePairingCode(t0);
  const edge = await reg.pair(c2, "x", new Date(t0.getTime() + 10 * 60_000 - 1000));
  assert.ok(edge.ok, edge.reason);
});

test("pairing: activePairingCode exposes the pending code only", async (t) => {
  const { reg, cleanup } = freshRegistry();
  t.after(cleanup);
  const t0 = new Date("2026-09-22T08:00:00Z");
  assert.equal(reg.activePairingCode(t0), null);
  const { code } = reg.generatePairingCode(t0);
  assert.equal(reg.activePairingCode(t0)?.code, code);
  await reg.pair(code, "dev", t0);
  assert.equal(reg.activePairingCode(t0), null); // consumed
});

test("devices: token auth, revoke and sync touch", async (t) => {
  const { reg, cleanup } = freshRegistry();
  t.after(cleanup);
  const t0 = new Date("2026-09-22T08:00:00Z");
  const { code } = reg.generatePairingCode(t0);
  const out = await reg.pair(code, "dev1", t0);
  const token = out.token!;
  const id = out.device!.id;

  assert.ok(reg.authenticate(token));
  assert.equal(reg.authenticate("deadbeef"), null);
  assert.equal(reg.authenticate(undefined), null);

  await reg.touchSync(id, new Date(t0.getTime() + 3_600_000));
  assert.equal(reg.list()[0]!.lastSyncAt, new Date(t0.getTime() + 3_600_000).toISOString());

  // revoked tokens stop authenticating immediately
  assert.equal(await reg.revoke(id), true);
  assert.equal(reg.authenticate(token), null);
  assert.equal(reg.list()[0]!.status, "revoked");
  assert.equal(await reg.revoke("nope"), false);
});

test("pairing: format validation", () => {
  assert.equal(DeviceRegistry.isValidCodeFormat("123456"), true);
  assert.equal(DeviceRegistry.isValidCodeFormat("12345"), false);
  assert.equal(DeviceRegistry.isValidCodeFormat("1234567"), false);
  assert.equal(DeviceRegistry.isValidCodeFormat("12345a"), false);
  assert.equal(DeviceRegistry.isValidCodeFormat(123456), false);
});

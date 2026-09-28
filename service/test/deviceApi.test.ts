import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync, statSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import Fastify from "fastify";
import { registerDeviceApi } from "../src/deviceApi.js";
import { DeviceRegistry } from "../src/store/devices.js";
import type { SnapshotService } from "../src/snapshotService.js";
import type { AssetPublisher } from "../src/assets/publisher.js";

function buildApp(dir: string) {
  const devices = new DeviceRegistry(dir);
  const snapshots = { get: async () => ({ schema: 1 }) } as unknown as SnapshotService;
  const publisher = { readBundle: async () => null } as unknown as AssetPublisher;
  const app = Fastify();
  registerDeviceApi(app, { devices, snapshots, publisher });
  return { app, devices };
}

test("device api: wrong pairing codes are throttled before the space is exhausted", async () => {
  const dir = mkdtempSync(path.join(tmpdir(), "device-api-"));
  const { app, devices } = buildApp(dir);
  try {
    devices.generatePairingCode(new Date());
    // Well-formed but wrong codes count toward the failure budget.
    for (let i = 0; i < 10; i++) {
      const r = await app.inject({ method: "POST", url: "/api/pair",
        payload: { code: "000000", deviceName: "pendant" } });
      assert.equal(r.statusCode, 403);
    }
    // Even a fresh, valid code is refused from a throttled client.
    const blocked = await app.inject({ method: "POST", url: "/api/pair",
      payload: { code: "000001", deviceName: "pendant" } });
    assert.equal(blocked.statusCode, 429);
  } finally {
    await app.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

test("device api: valid code pairs, snapshot requires the device token, registry is 0600", async () => {
  const dir = mkdtempSync(path.join(tmpdir(), "device-api-"));
  const { app, devices } = buildApp(dir);
  try {
    const { code } = devices.generatePairingCode(new Date());
    const wrong = code === "000000" ? "000001" : "000000";
    const refused = await app.inject({ method: "POST", url: "/api/pair",
      payload: { code: wrong, deviceName: "pendant" } });
    assert.equal(refused.statusCode, 403);

    const unauth = await app.inject({ method: "GET", url: "/api/snapshot" });
    assert.equal(unauth.statusCode, 401);

    const paired = await app.inject({ method: "POST", url: "/api/pair",
      payload: { code, deviceName: "pendant" } });
    assert.equal(paired.statusCode, 200);
    const token = paired.json().token as string;
    assert.ok(token);

    const snap = await app.inject({ method: "GET", url: "/api/snapshot",
      headers: { "x-device-token": token } });
    assert.equal(snap.statusCode, 200);
    assert.equal(snap.json().schema, 1);

    // Bearer tokens land in devices.json; it must not be world-readable.
    const mode = statSync(path.join(dir, "devices.json")).mode & 0o777;
    assert.equal(mode, 0o600);
  } finally {
    await app.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import Fastify from "fastify";
import { companionIdentity, registerCompanionApi } from "../src/companionApi.js";
import { DeviceRegistry } from "../src/store/devices.js";
import type { AppDeps } from "../src/app.js";

test("companion is loopback only, rejects browser requests, scopes snapshots and persists stable identities", async () => {
  const dir = mkdtempSync(path.join(tmpdir(), "passport-ble-api-"));
  const other = mkdtempSync(path.join(tmpdir(), "passport-other-host-"));
  const app = Fastify();
  const devices = new DeviceRegistry(dir);
  const host = companionIdentity(dir);
  assert.equal(companionIdentity(dir), host);
  const otherHost = companionIdentity(other);
  assert.notEqual(otherHost, host);
  let oversized = false;
  registerCompanionApi(app, { devices, snapshots: { get: async () => ({ schema: 1, pad: oversized ? "x".repeat(4096) : "" }) } } as unknown as AppDeps, host);
  try {
    const query = (mac: string, owner = host) => `/api/companion/${mac}/snapshot?host=${owner}`;
    assert.equal((await app.inject({ url: "/api/companion", remoteAddress: "10.0.0.5" })).statusCode, 403);
    assert.equal((await app.inject({ url: "/api/companion", headers: { origin: "https://example.com" } })).statusCode, 403);
    assert.equal((await app.inject({ url: "/api/companion", headers: { "sec-fetch-site": "cross-site" } })).statusCode, 403);
    assert.equal((await app.inject({ url: "/api/companion" })).json().host, host);
    assert.equal((await app.inject({ url: query("AA:BB:CC:DD:EE:01", otherHost) })).statusCode, 400);
    assert.equal((await app.inject({ url: query("not-a-mac") })).statusCode, 400);
    const first = await app.inject({ url: query("AA:BB:CC:DD:EE:01") });
    assert.equal(first.statusCode, 200);
    assert.deepEqual(first.json().source, { device: "AA:BB:CC:DD:EE:01", host });
    await app.inject({ url: query("AA:BB:CC:DD:EE:02") });
    await app.inject({ url: query("AA:BB:CC:DD:EE:01") });
    assert.equal(devices.list().length, 2); // reconnect never creates another identity
    assert.equal(new DeviceRegistry(dir).list().length, 2);
    const record = devices.list().find(d => d.hardwareId === "AA:BB:CC:DD:EE:01")!;
    await devices.revoke(record.id);
    assert.equal((await app.inject({ url: query("AA:BB:CC:DD:EE:01") })).statusCode, 403);
    assert.equal((await app.inject({ url: `/api/companion/AA:BB:CC:DD:EE:01/authorization?host=${host}` })).statusCode, 403);
    assert.equal(await devices.allowBlePairing(record.id), true);
    assert.equal((await app.inject({ url: `/api/companion/AA:BB:CC:DD:EE:01/authorization?host=${host}` })).statusCode, 200);
    oversized = true;
    assert.equal((await app.inject({ url: query("AA:BB:CC:DD:EE:02") })).statusCode, 413);
    assert.equal((await devices.registerBle("AA:BB:CC:DD:EE:02", otherHost, new Date()))!.hostId, otherHost);
    assert.equal(devices.list().length, 3); // same hardware on another endpoint is distinct
  } finally { await app.close(); rmSync(dir, { recursive: true, force: true }); rmSync(other, { recursive: true, force: true }); }
});

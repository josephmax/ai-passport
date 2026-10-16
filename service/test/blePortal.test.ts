import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import Fastify from "fastify";
import cookie from "@fastify/cookie";
import formbody from "@fastify/formbody";
import { registerPortal, type PortalDeps } from "../src/portal/routes.js";
import { DeviceRegistry } from "../src/store/devices.js";

test("BLE portal shows both identities and protects revoke/re-enable with portal login", async () => {
  const dir = mkdtempSync(path.join(tmpdir(), "passport-ble-portal-"));
  const app = Fastify();
  const devices = new DeviceRegistry(dir);
  const host = "12345678-1234-1234-1234-123456789abc";
  const record = (await devices.registerBle("AA:BB:CC:DD:EE:01", host, new Date()))!;
  try {
    await app.register(cookie, { secret: "test-ble-portal-secret" });
    await app.register(formbody);
    registerPortal(app, { auth: { password: "test-password", secret: "test-ble-portal-secret" }, devices } as PortalDeps);
    const login = await app.inject({ method: "POST", url: "/portal/login", payload: { password: "test-password" } });
    const headers = { cookie: String(login.headers["set-cookie"]).split(";")[0]! };
    const page = await app.inject({ url: "/portal/devices", headers });
    assert.equal(page.statusCode, 200);
    assert.match(page.body, /AA:BB:CC:DD:EE:01/);
    assert.match(page.body, new RegExp(host));
    assert.match(page.body, /<details><summary>/);
    assert.doesNotMatch(page.body, new RegExp(record.token));
    await app.inject({ method: "POST", url: "/portal/devices/revoke", headers, payload: { deviceId: record.id } });
    assert.equal(devices.list()[0]!.status, "revoked");
    await app.inject({ method: "POST", url: "/portal/devices/allow-ble", payload: { deviceId: record.id } });
    assert.equal(devices.list()[0]!.status, "revoked");
    await app.inject({ method: "POST", url: "/portal/devices/allow-ble", headers, payload: { deviceId: record.id } });
    assert.equal(devices.list()[0]!.status, "active");
  } finally { await app.close(); rmSync(dir, { recursive: true, force: true }); }
});

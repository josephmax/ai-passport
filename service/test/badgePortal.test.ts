import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import Fastify from "fastify";
import cookie from "@fastify/cookie";
import formbody from "@fastify/formbody";
import { registerPortal, type PortalDeps } from "../src/portal/routes.js";
import { SettingsStore } from "../src/store/settings.js";

test("badge portal saves a renderable name and refreshes the device snapshot", async () => {
  const dir = mkdtempSync(path.join(tmpdir(), "passport-badge-"));
  const settings = new SettingsStore(dir);
  let refreshes = 0;
  const app = Fastify();
  try {
    await app.register(cookie, { secret: "test-secret-long-enough" });
    await app.register(formbody);
    registerPortal(app, {
      auth: { password: "test-password", secret: "test-secret-long-enough" },
      settings,
      refreshSnapshot: async () => { refreshes++; },
    } as PortalDeps);

    const login = await app.inject({ method: "POST", url: "/portal/login", payload: { password: "test-password" } });
    assert.equal(login.statusCode, 302);
    const session = login.headers["set-cookie"];
    assert.ok(session);
    const headers = { cookie: String(session).split(";")[0]! };

    const saved = await app.inject({ method: "POST", url: "/portal/badge", headers, payload: { badgeName: "Joseph", badgeRole: "Developer" } });
    assert.equal(saved.statusCode, 302);
    assert.equal(settings.get().badgeName, "Joseph");
    assert.equal(settings.get().badgeRole, "Developer");
    assert.equal(refreshes, 1);

    const rejected = await app.inject({ method: "POST", url: "/portal/badge", headers, payload: { badgeName: "未收录", badgeRole: "Developer" } });
    assert.equal(rejected.statusCode, 302);
    assert.equal(settings.get().badgeName, "Joseph");
    assert.equal(refreshes, 1);
  } finally {
    await app.close();
    rmSync(dir, { recursive: true, force: true });
  }
});

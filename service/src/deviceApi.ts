/**
 * Device-facing API (LAN):
 *   POST /api/pair                     pairing code -> device token
 *   GET  /api/snapshot                 X-Device-Token -> snapshot JSON (§4.3)
 *   GET  /assets/bundle_v{N}.bin       published skin bundle (APB1 binary)
 *   GET  /api/health                   liveness
 *
 * The asset download carries no auth header in the device protocol (spec §8);
 * it is version-gated and LAN-only. Pairing codes: 6 digits / 10 min / one-shot.
 */

import type { FastifyInstance } from "fastify";
import { DeviceRegistry } from "./store/devices.js";
import type { SnapshotService } from "./snapshotService.js";
import type { AssetPublisher } from "./assets/publisher.js";
import { FailureLimiter } from "./util/rateLimit.js";

export interface DeviceApiDeps {
  devices: DeviceRegistry;
  snapshots: SnapshotService;
  publisher: AssetPublisher;
}

export function registerDeviceApi(app: FastifyInstance, deps: DeviceApiDeps): void {
  const { devices, snapshots, publisher } = deps;
  // 6-digit codes are brute-forceable inside their 10-minute TTL (~1.7k req/s
  // exhausts the space); cap failed attempts per client IP instead. A
  // legitimate pairing needs exactly one attempt.
  const pairFailures = new FailureLimiter(10, 10 * 60_000);

  app.get("/api/health", async () => ({ ok: true, service: "ai-passport-local-service", now: new Date().toISOString() }));

  app.post<{ Body: { code?: unknown; deviceName?: unknown } }>(
    "/api/pair",
    async (request, reply) => {
      const now = new Date();
      if (pairFailures.isBlocked(request.ip, now)) {
        await reply.code(429).send({ error: "too many failed attempts; retry later" });
        return;
      }
      const body = request.body ?? {};
      const { code, deviceName } = body as { code?: unknown; deviceName?: unknown };
      if (typeof deviceName !== "string" || deviceName.trim().length === 0) {
        await reply.code(400).send({ error: "deviceName required" });
        return;
      }
      if (!DeviceRegistry.isValidCodeFormat(code)) {
        pairFailures.recordFailure(request.ip, now);
        await reply.code(403).send({ error: "invalid pairing code" });
        return;
      }
      const outcome = await devices.pair(code, deviceName.trim(), now);
      if (!outcome.ok) {
        pairFailures.recordFailure(request.ip, now);
        await reply.code(403).send({ error: `pairing code ${outcome.reason}` });
        return;
      }
      pairFailures.reset(request.ip);
      await reply.code(200).send({ token: outcome.token });
    },
  );

  app.get("/api/snapshot", async (request, reply) => {
    const token = request.headers["x-device-token"];
    const tokenStr = Array.isArray(token) ? token[0] : token;
    const device = devices.authenticate(tokenStr);
    if (!device) {
      await reply.code(401).send({ error: "unauthorized" });
      return;
    }
    const snapshot = await snapshots.get();
    // Fire-and-forget, but a transient disk error must never take down the
    // process (unhandled rejection terminates Node >= 15).
    devices.touchSync(device.id, new Date()).catch((err) => {
      console.warn("[devices] last-sync save failed:", err instanceof Error ? err.message : err);
    });
    await reply.code(200).send(snapshot);
  });

  app.get("/assets/:name", async (request, reply) => {
    const params = request.params as { name?: string };
    const m = params.name ? /^bundle_v(\d+)\.bin$/.exec(params.name) : null;
    if (!m || !m[1]) {
      await reply.code(404).send({ error: "not found" });
      return;
    }
    const version = Number(m[1]);
    const buf = await publisher.readBundle(version);
    if (!buf) {
      await reply.code(404).send({ error: "bundle version not published" });
      return;
    }
    await reply
      .code(200)
      .header("content-type", "application/octet-stream")
      .header("content-length", String(buf.length))
      .send(buf);
  });
}

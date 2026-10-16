/** Local desktop sidecar. The device never reaches this API over Wi-Fi. */
import { randomUUID } from "node:crypto";
import path from "node:path";
import type { FastifyInstance } from "fastify";
import type { AppDeps } from "./app.js";
import { readJson, writeJsonSync } from "./store/jsonStore.js";

export function companionIdentity(dataDir: string): string {
  const file = path.join(dataDir, "companion-host.json");
  const stored = readJson<{ id: string }>(file, { id: "" });
  if (/^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$/.test(stored.id)) return stored.id;
  const id = randomUUID();
  writeJsonSync(file, { id }, 0o600);
  return id;
}

export function registerCompanionApi(app: FastifyInstance, deps: AppDeps, hostId: string): void {
  app.register(async local => {
    local.addHook("onRequest", async (request, reply) => {
      // Do not trust Host/X-Forwarded-For or let browser pages invoke the sidecar.
      const peer = request.raw.socket.remoteAddress;
      if (!["127.0.0.1", "::1", "::ffff:127.0.0.1"].includes(peer ?? "") ||
          request.headers.origin || request.headers["sec-fetch-site"]) {
        await reply.code(403).send({ error: "desktop loopback access required" });
      }
    });
    local.get("/api/companion", async () => ({ v: 1, host: hostId }));
    local.get<{ Params: { mac: string }; Querystring: { host?: string } }>(
      "/api/companion/:mac/authorization", async (request, reply) => {
        const mac = request.params.mac.toUpperCase();
        if (!/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(mac) || request.query.host !== hostId) {
          return reply.code(400).send({ error: "identity mismatch" });
        }
        const record = deps.devices.list().find(d => d.transport === "ble" && d.hardwareId === mac && d.hostId === hostId);
        return record?.status === "revoked" ? reply.code(403).send({ error: "device revoked" }) : { allowed: true };
      });
    local.get<{ Params: { mac: string }; Querystring: { host?: string } }>(
      "/api/companion/:mac/snapshot", async (request, reply) => {
        const mac = request.params.mac.toUpperCase();
        if (!/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(mac) || request.query.host !== hostId) {
          return reply.code(400).send({ error: "device MAC and matching host identity required" });
        }
        const record = await deps.devices.registerBle(mac, hostId, new Date());
        if (!record) return reply.code(403).send({ error: "device revoked" });
        // Each local service is one data source. Device/host keys are separate;
        // firmware replaces the selected source rather than summing snapshots.
        const snapshot = { ...await deps.snapshots.get(), source: { device: mac, host: hostId } };
        if (Buffer.byteLength(JSON.stringify(snapshot)) >= 4096) {
          return reply.code(413).send({ error: "snapshot exceeds firmware limit" });
        }
        return snapshot;
      });
  });
}

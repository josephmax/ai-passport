/** Fastify app factory: device API + config portal on one port. */

import Fastify, { type FastifyInstance } from "fastify";
import cookie from "@fastify/cookie";
import formbody from "@fastify/formbody";
import multipart from "@fastify/multipart";
import { registerDeviceApi } from "./deviceApi.js";
import { registerPortal } from "./portal/routes.js";
import type { SettingsStore } from "./store/settings.js";
import type { DeviceRegistry } from "./store/devices.js";
import type { SnapshotService } from "./snapshotService.js";
import type { AssetPublisher } from "./assets/publisher.js";
import type { DraftStore } from "./assets/draftStore.js";
import type { Collector } from "./collectors/types.js";

export interface AppDeps {
  auth: { password: string | null; secret: string };
  settings: SettingsStore;
  devices: DeviceRegistry;
  snapshots: SnapshotService;
  publisher: AssetPublisher;
  draft: DraftStore;
  collectors: Collector[];
}

export async function buildApp(deps: AppDeps): Promise<FastifyInstance> {
  const app = Fastify({
    logger: false,
    bodyLimit: 1 * 1024 * 1024, // JSON bodies (pair) stay small
  });

  await app.register(cookie, { secret: deps.auth.secret });
  await app.register(formbody);
  await app.register(multipart, {
    limits: {
      fileSize: 2 * 1024 * 1024, // per PNG
      files: 12,
    },
  });

  app.get("/", async (_request, reply) => reply.redirect("/portal"));

  registerDeviceApi(app, {
    devices: deps.devices,
    snapshots: deps.snapshots,
    publisher: deps.publisher,
  });

  registerPortal(app, {
    auth: deps.auth,
    settings: deps.settings,
    devices: deps.devices,
    collectors: deps.collectors,
    draft: deps.draft,
    publisher: deps.publisher,
    refreshSnapshot: () => deps.snapshots.refresh(),
  });

  return app;
}

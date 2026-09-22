/**
 * Local service entry point.
 *
 *   npm run build && npm start     (node dist/index.js)
 *   npm run dev                    (tsx watch)
 *
 * Environment (.env, see .env.example): PORT, HOST, PORTAL_PASSWORD,
 * SESSION_SECRET, CLAUDE_PROJECTS_PATH, SNAPSHOT_REFRESH_MINUTES, DATA_DIR.
 */

import path from "node:path";
import { randomBytes } from "node:crypto";
import { fileURLToPath } from "node:url";
import dotenv from "dotenv";
import { mkdir } from "node:fs/promises";

const serviceRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
dotenv.config({ path: path.join(serviceRoot, ".env") });
dotenv.config(); // also honor cwd .env if present

import { buildApp } from "./app.js";
import { SettingsStore } from "./store/settings.js";
import { DeviceRegistry } from "./store/devices.js";
import { WeatherService } from "./weather.js";
import { SnapshotService } from "./snapshotService.js";
import { ClaudeCollector } from "./collectors/claude.js";
import { GlmCollector } from "./collectors/glm.js";
import { DeepSeekCollector } from "./collectors/deepseek.js";
import { AssetPublisher } from "./assets/publisher.js";
import { DraftStore } from "./assets/draftStore.js";

async function main(): Promise<void> {
  const dataDir = process.env.DATA_DIR
    ? path.resolve(process.env.DATA_DIR)
    : path.join(serviceRoot, "data");
  const assetsDir = path.join(dataDir, "assets");
  await mkdir(assetsDir, { recursive: true });

  const port = Number(process.env.PORT ?? 3000);
  const host = process.env.HOST ?? "0.0.0.0";
  const portalPassword = process.env.PORTAL_PASSWORD?.trim() || null;
  let sessionSecret = process.env.SESSION_SECRET?.trim() || "";
  if (!sessionSecret) {
    sessionSecret = randomBytes(32).toString("hex");
    console.warn("[env] SESSION_SECRET not set; using an ephemeral secret (portal logins reset on restart)");
  }
  if (!portalPassword) {
    console.warn("[env] PORTAL_PASSWORD not set; config portal login is disabled (device API unaffected)");
  }

  const settings = new SettingsStore(dataDir);
  const devices = new DeviceRegistry(dataDir);
  const publisher = new AssetPublisher(assetsDir);
  const draft = new DraftStore(assetsDir);

  // Auto-publish the placeholder bundle as v1 so fresh devices get assets.
  const placeholder = await publisher.ensurePlaceholderPublished();
  if (placeholder) {
    console.log(`[assets] published default placeholder bundle v${placeholder.version}`);
  }

  const s = settings.get();
  const claudeCollector = new ClaudeCollector({
    label: s.claude.label,
    projectsPath: process.env.CLAUDE_PROJECTS_PATH ?? s.claude.projectsPath,
    caps: {
      weeklyCapHours: s.claude.weeklyCapHours,
      rolling5hCapHours: s.claude.rolling5hCapHours,
    },
  });
  const glmCollector = new GlmCollector({
    label: s.glm.label,
    url: s.glm.codingPlanUrl,
    getKey: () => settings.getKey("glm"),
  });
  const deepseekCollector = new DeepSeekCollector({
    label: s.deepseek.label,
    url: s.deepseek.balanceUrl,
    getKey: () => settings.getKey("deepseek"),
  });
  const collectors = [claudeCollector, glmCollector, deepseekCollector];

  const weather = new WeatherService();
  const snapshots = new SnapshotService({
    collectors,
    weather,
    settings,
    getAssetBundleVersion: () => publisher.currentVersion(),
    refreshMinutes: Number(process.env.SNAPSHOT_REFRESH_MINUTES ?? 10),
    log: (m) => console.log(m),
  });

  const app = await buildApp({
    auth: { password: portalPassword, secret: sessionSecret },
    settings,
    devices,
    snapshots,
    publisher,
    draft,
    collectors,
  });

  snapshots.start();

  await app.listen({ port, host });
  console.log(`[service] listening on http://${host}:${port}`);
  console.log(`[service] data dir: ${dataDir}`);
  console.log(`[service] asset bundle version: v${publisher.currentVersion()}`);
  console.log(`[service] config portal: http://<lan-ip>:${port}/portal`);

  const shutdown = async (signal: string) => {
    console.log(`[service] ${signal} received, shutting down`);
    snapshots.stop();
    await app.close().catch(() => undefined);
    process.exit(0);
  };
  process.on("SIGINT", () => void shutdown("SIGINT"));
  process.on("SIGTERM", () => void shutdown("SIGTERM"));
}

main().catch((err) => {
  console.error("[service] fatal:", err);
  process.exit(1);
});

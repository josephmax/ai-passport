/** Private per-user state for the self-contained macOS application. */
import { randomBytes } from "node:crypto";
import { chmodSync, mkdirSync, readFileSync, realpathSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import dotenv from "dotenv";

export function prepareDesktop(root) {
  if (!path.isAbsolute(root)) throw new Error("Desktop data path must be absolute");
  mkdirSync(root, { recursive: true, mode: 0o700 });
  chmodSync(root, 0o700);
  const envFile = path.join(root, ".env");
  try {
    writeFileSync(envFile, `PORTAL_PASSWORD=${randomBytes(18).toString("hex")}\nSESSION_SECRET=${randomBytes(32).toString("hex")}\n`, { flag: "wx", mode: 0o600 });
  } catch (error) { if (error.code !== "EEXIST") throw error; }
  chmodSync(envFile, 0o600);
  const config = dotenv.parse(readFileSync(envFile));
  if (!/^[0-9a-f]{36}$/.test(config.PORTAL_PASSWORD ?? "") || !/^[0-9a-f]{64}$/.test(config.SESSION_SECRET ?? "")) {
    throw new Error("Desktop .env is invalid; refusing to reset credentials");
  }
  return { ...config, DATA_DIR: path.join(root, "data"), HOST: "127.0.0.1", PORT: "0", PASSPORT_COMPANION_MANAGED: "1" };
}

// Importing this module for tests must not launch a server.
if (process.argv[1] && realpathSync(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.umask(0o077);
  Object.assign(process.env, prepareDesktop(process.argv[2] ?? ""));
  // An app crash closes the pipe as well; a sidecar must not survive its owner.
  process.stdin.resume();
  process.stdin.on("end", () => process.kill(process.pid, "SIGTERM"));
  process.stdin.on("error", () => process.kill(process.pid, "SIGTERM"));
  await import("../dist/index.js");
}

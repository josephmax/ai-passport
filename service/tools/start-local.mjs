/** Start a detached service for local acceptance, preserving existing settings. */
import { spawn } from "node:child_process";
import { randomBytes } from "node:crypto";
import { existsSync, mkdirSync, openSync, closeSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import dotenv from "dotenv";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const envFile = path.join(root, ".env");
if (!existsSync(envFile)) {
  writeFileSync(envFile, `PORT=3000\nHOST=0.0.0.0\nPORTAL_PASSWORD=${randomBytes(18).toString("hex")}\nSESSION_SECRET=${randomBytes(32).toString("hex")}\n`, { mode: 0o600, flag: "wx" });
  console.log("Created private service/.env; read PORTAL_PASSWORD there to log in.");
}
dotenv.config({ path: envFile, quiet: true });
const port = Number(process.env.PORT ?? 3000);
if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error("Invalid PORT");
let existing;
try { existing = await fetch(`http://127.0.0.1:${port}/api/health`, { signal: AbortSignal.timeout(1000) }); }
catch { /* Start when not reachable. */ }
if (existing) {
  const health = await existing.json().catch(() => null);
  if (existing.ok && health?.service === "ai-passport-local-service") {
    console.log(`Service already reachable at http://localhost:${port}/portal/accounts`);
    process.exit(0);
  }
  throw new Error("Configured port is occupied by another HTTP service; choose another PORT in service/.env");
}
const dataDir = process.env.DATA_DIR ? path.resolve(root, process.env.DATA_DIR) : path.join(root, "data");
mkdirSync(dataDir, { recursive: true });
const fd = openSync(path.join(dataDir, "service.log"), "a", 0o600);
const child = spawn(process.execPath, [path.join(root, "dist/index.js")], {
  cwd: root, env: process.env, detached: true, stdio: ["ignore", fd, fd],
});
closeSync(fd);
child.on("error", err => { console.error(err.message); process.exitCode = 1; });
child.unref();
writeFileSync(path.join(dataDir, "service.pid"), String(child.pid), { mode: 0o600 });
let ready = false;
for (let attempt = 0; attempt < 120; attempt++) {
  await new Promise(resolve => setTimeout(resolve, 250));
  try {
    const response = await fetch(`http://127.0.0.1:${port}/api/health`, { signal: AbortSignal.timeout(1000) });
    if (response.ok) { ready = true; break; }
  } catch { /* Wait for startup. */ }
}
if (!ready) throw new Error("Service did not start; inspect service/data/service.log");
console.log(`Service running at http://localhost:${port}/portal/accounts (PID ${child.pid}).`);
console.log("It remains running after this command exits; host sleep/reboot interrupts availability.");

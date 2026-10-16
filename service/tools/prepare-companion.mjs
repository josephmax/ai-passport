/** Prepare a private local configuration without printing any credentials. */
import { randomBytes } from "node:crypto";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import dotenv from "dotenv";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const file = path.join(root, ".env");
if (!existsSync(file)) {
  try {
    writeFileSync(file, `PORT=3000\nHOST=127.0.0.1\nPORTAL_PASSWORD=${randomBytes(18).toString("hex")}\nSESSION_SECRET=${randomBytes(32).toString("hex")}\n`, { mode: 0o600, flag: "wx" });
    console.error("Created private service/.env; read PORTAL_PASSWORD there to log in.");
  } catch (error) { if (error.code !== "EEXIST") throw error; }
}
const config = dotenv.parse(readFileSync(file));
const port = Number(process.env.PORT ?? config.PORT ?? 3000);
if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error("Invalid service PORT");
console.log(port);

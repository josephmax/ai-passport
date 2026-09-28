/**
 * Device registry + one-time pairing codes.
 *
 * Pairing code rules (spec config-portal §2.5/§3): 6 digits, valid 10 minutes,
 * single use. Device tokens are 32 hex chars (16 random bytes), revocable.
 * Codes live in memory only (a service restart invalidates pending codes,
 * which is acceptable and documented); devices persist to data/devices.json.
 */

import path from "node:path";
import { randomBytes, randomInt } from "node:crypto";
import { readJson, writeJson } from "./jsonStore.js";

export const PAIRING_CODE_TTL_MS = 10 * 60_000;

export interface DeviceRecord {
  id: string;
  name: string;
  token: string;
  status: "active" | "revoked";
  pairedAt: string; // RFC3339
  lastSyncAt: string | null;
}

interface DevicesFile {
  devices: DeviceRecord[];
}

interface PendingCode {
  code: string;
  createdAt: number;
  expiresAt: number;
  usedAt: number | null;
}

export interface PairOutcome {
  ok: boolean;
  reason?: "invalid" | "expired" | "used";
  token?: string;
  device?: DeviceRecord;
}

function pad6(n: number): string {
  return String(n).padStart(6, "0");
}

export class DeviceRegistry {
  private readonly file: string;
  private devices: DeviceRecord[];
  private pending = new Map<string, PendingCode>();

  constructor(dataDir: string) {
    this.file = path.join(dataDir, "devices.json");
    const loaded = readJson<DevicesFile>(this.file, { devices: [] });
    this.devices = Array.isArray(loaded.devices) ? loaded.devices : [];
  }

  private async save(): Promise<void> {
    // Holds bearer tokens; keep the same 0600 protection as keys.json.
    await writeJson(this.file, { devices: this.devices }, 0o600);
  }

  // ---- pairing codes -------------------------------------------------------

  generatePairingCode(now: Date): { code: string; expiresAt: number } {
    // drop expired/used codes
    for (const [k, c] of this.pending) {
      if (c.usedAt !== null || c.expiresAt <= now.getTime()) this.pending.delete(k);
    }
    let code = pad6(randomInt(0, 1_000_000));
    let guard = 0;
    while (this.pending.has(code) && guard++ < 20) {
      code = pad6(randomInt(0, 1_000_000));
    }
    const entry: PendingCode = {
      code,
      createdAt: now.getTime(),
      expiresAt: now.getTime() + PAIRING_CODE_TTL_MS,
      usedAt: null,
    };
    this.pending.set(code, entry);
    return { code, expiresAt: entry.expiresAt };
  }

  activePairingCode(now: Date): { code: string; expiresAt: number } | null {
    for (const c of this.pending.values()) {
      if (c.usedAt === null && c.expiresAt > now.getTime()) {
        return { code: c.code, expiresAt: c.expiresAt };
      }
    }
    return null;
  }

  // ---- pairing -------------------------------------------------------------

  /** Validate code format: exactly 6 digits. */
  static isValidCodeFormat(code: unknown): code is string {
    return typeof code === "string" && /^\d{6}$/.test(code);
  }

  async pair(code: string, deviceName: string, now: Date): Promise<PairOutcome> {
    const entry = this.pending.get(code);
    if (!entry) return { ok: false, reason: "invalid" };
    if (entry.usedAt !== null) return { ok: false, reason: "used" };
    if (entry.expiresAt <= now.getTime()) return { ok: false, reason: "expired" };
    entry.usedAt = now.getTime();
    const device: DeviceRecord = {
      id: randomBytes(4).toString("hex"),
      name: deviceName.slice(0, 64),
      token: randomBytes(16).toString("hex"), // 32 hex chars
      status: "active",
      pairedAt: now.toISOString(),
      lastSyncAt: null,
    };
    this.devices.push(device);
    await this.save();
    return { ok: true, token: device.token, device };
  }

  // ---- devices -------------------------------------------------------------

  list(): DeviceRecord[] {
    return [...this.devices];
  }

  /** Authenticate by token; returns the device if active. */
  authenticate(token: string | undefined | null): DeviceRecord | null {
    if (!token) return null;
    const dev = this.devices.find((d) => d.token === token);
    if (!dev || dev.status !== "active") return null;
    return dev;
  }

  async touchSync(deviceId: string, now: Date): Promise<void> {
    const dev = this.devices.find((d) => d.id === deviceId);
    if (dev) {
      dev.lastSyncAt = now.toISOString();
      await this.save();
    }
  }

  async revoke(deviceId: string): Promise<boolean> {
    const dev = this.devices.find((d) => d.id === deviceId);
    if (!dev) return false;
    dev.status = "revoked";
    await this.save();
    return true;
  }
}

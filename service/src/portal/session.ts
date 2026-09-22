/**
 * Config-portal sessions: signed cookie (sessionId) + in-memory session map.
 * Password comes from PORTAL_PASSWORD (.env). Sessions expire after 8h of
 * inactivity. P1 runs on the LAN behind a password; CSRF protection is
 * documented as out of scope for P1 (see README security notes).
 */

import { createHash, randomBytes, timingSafeEqual } from "node:crypto";
import type { FastifyInstance, FastifyRequest } from "fastify";

export const SESSION_COOKIE = "pp_session";
const SESSION_TTL_MS = 8 * 3_600_000;

export interface PortalAuth {
  password: string | null;
  /** Value to sign cookies with; ephemeral when SESSION_SECRET is unset. */
  secret: string;
}

export class SessionStore {
  private sessions = new Map<string, number>(); // id -> expiresAtMs

  create(now: Date): string {
    const id = randomBytes(24).toString("hex");
    this.sessions.set(id, now.getTime() + SESSION_TTL_MS);
    return id;
  }

  isValid(id: string | undefined, now: Date): boolean {
    if (!id) return false;
    const exp = this.sessions.get(id);
    if (exp === undefined) return false;
    if (exp <= now.getTime()) {
      this.sessions.delete(id);
      return false;
    }
    this.sessions.set(id, now.getTime() + SESSION_TTL_MS); // sliding
    return true;
  }

  destroy(id: string | undefined): void {
    if (id) this.sessions.delete(id);
  }
}

export function hashPassword(pw: string): Buffer {
  return createHash("sha256").update(pw, "utf8").digest();
}

export function checkPassword(given: string, expected: string | null): boolean {
  if (!expected) return false; // PORTAL_PASSWORD unset -> login disabled
  const a = hashPassword(given);
  const b = hashPassword(expected);
  return a.length === b.length && timingSafeEqual(a, b);
}

export function readSessionCookie(request: FastifyRequest): string | undefined {
  const raw = request.cookies[SESSION_COOKIE];
  if (!raw) return undefined;
  const unsigned = request.unsignCookie(raw);
  return unsigned.valid && typeof unsigned.value === "string" ? unsigned.value : undefined;
}

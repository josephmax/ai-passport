import { readFileSync } from "node:fs";

// The firmware's 24 px font is generated from this same inventory. Keep
// backend validation aligned so an unsupported name cannot become □ on screen.
// Loaded lazily: a missing inventory must not crash the whole service at boot.
let cjkGlyphs: string | null = null;

function glyphs(): string {
  if (cjkGlyphs === null) {
    try {
      cjkGlyphs = readFileSync(new URL("../../../main/fonts/badge_name_glyphs.txt", import.meta.url), "utf8").trim();
    } catch (err) {
      // Without the inventory CJK names cannot be verified; validation fails
      // closed for them (rejected) instead of risking □ on the badge.
      console.warn("[badge] badge_name_glyphs.txt unreadable; CJK badge names will be rejected:",
        err instanceof Error ? err.message : err);
      cjkGlyphs = "";
    }
  }
  return cjkGlyphs;
}

export function validateBadgeName(raw: unknown): string | null {
  if (typeof raw !== "string") return null;
  const name = raw.trim();
  if (!name) return "";
  if (Buffer.byteLength(name, "utf8") >= 32) return null;
  let cells = 0;
  for (const ch of name) {
    if (/^[A-Za-z0-9 ._-]$/.test(ch)) cells += 1;
    else if (glyphs().includes(ch)) cells += 2;
    else return null;
  }
  return cells <= 8 ? name : null;
}

export function validateBadgeRole(raw: unknown): string | null {
  if (typeof raw !== "string") return null;
  const role = raw.trim();
  return role.length <= 10 && /^[A-Za-z0-9 ._-]*$/.test(role) ? role : null;
}

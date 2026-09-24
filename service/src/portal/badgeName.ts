import { readFileSync } from "node:fs";

// The firmware's 24 px font is generated from this same inventory. Keep
// backend validation aligned so an unsupported name cannot become □ on screen.
const cjkGlyphs = readFileSync(new URL("../../../main/fonts/badge_name_glyphs.txt", import.meta.url), "utf8").trim();

export function validateBadgeName(raw: unknown): string | null {
  if (typeof raw !== "string") return null;
  const name = raw.trim();
  if (!name) return "";
  if (Buffer.byteLength(name, "utf8") >= 32) return null;
  let cells = 0;
  for (const ch of name) {
    if (/^[A-Za-z0-9 ._-]$/.test(ch)) cells += 1;
    else if (cjkGlyphs.includes(ch)) cells += 2;
    else return null;
  }
  return cells <= 8 ? name : null;
}

export function validateBadgeRole(raw: unknown): string | null {
  if (typeof raw !== "string") return null;
  const role = raw.trim();
  return role.length <= 10 && /^[A-Za-z0-9 ._-]*$/.test(role) ? role : null;
}

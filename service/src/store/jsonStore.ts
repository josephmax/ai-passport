/** Tiny atomic JSON file store (write to tmp + rename). */

import { chmod, mkdir, rename, writeFile } from "node:fs/promises";
import { chmodSync, existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from "node:fs";
import path from "node:path";

let tmpSeq = 0;

// Concurrent saves to one file (e.g. two devices polling /api/snapshot) must
// not interleave on a shared tmp name; pid + a monotonic suffix keeps each
// write private until its own rename.
function tmpPath(file: string): string {
  return `${file}.${process.pid}.${(tmpSeq++).toString(36)}.tmp`;
}

export function readJson<T>(file: string, fallback: T): T {
  try {
    const text = readFileSync(file, "utf8");
    const parsed = JSON.parse(text) as Partial<T>;
    // shallow-merge over fallback so new fields gain defaults
    return { ...(fallback as object), ...(parsed as object) } as T;
  } catch (err) {
    // ENOENT is the normal first-boot case. Anything else would otherwise
    // silently reset the file (a corrupt devices.json un-pairs every device).
    if ((err as NodeJS.ErrnoException).code !== "ENOENT") {
      console.warn(`[store] ${file} unreadable, using defaults:`,
        err instanceof Error ? err.message : err);
    }
    return fallback;
  }
}

function serialize(value: unknown): string {
  return JSON.stringify(value, null, 2) + "\n";
}

export async function writeJson(file: string, value: unknown, mode?: number): Promise<void> {
  await mkdir(path.dirname(file), { recursive: true });
  const tmp = tmpPath(file);
  await writeFile(tmp, serialize(value), "utf8");
  if (mode !== undefined) {
    await chmod(tmp, mode).catch(() => undefined);
  }
  await rename(tmp, file);
}

/** Synchronous variant for constructors (e.g. settings migration on load). */
export function writeJsonSync(file: string, value: unknown, mode?: number): void {
  mkdirSync(path.dirname(file), { recursive: true });
  const tmp = tmpPath(file);
  writeFileSync(tmp, serialize(value), "utf8");
  if (mode !== undefined) {
    try { chmodSync(tmp, mode); } catch { /* best-effort, mirrors async path */ }
  }
  renameSync(tmp, file);
}

export function exists(file: string): boolean {
  return existsSync(file);
}

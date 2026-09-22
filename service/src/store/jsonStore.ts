/** Tiny atomic JSON file store (write to tmp + rename). */

import { mkdir, rename, writeFile, chmod } from "node:fs/promises";
import { existsSync, readFileSync } from "node:fs";
import path from "node:path";

export function readJson<T>(file: string, fallback: T): T {
  try {
    const text = readFileSync(file, "utf8");
    const parsed = JSON.parse(text) as Partial<T>;
    // shallow-merge over fallback so new fields gain defaults
    return { ...(fallback as object), ...(parsed as object) } as T;
  } catch {
    return fallback;
  }
}

export async function writeJson(file: string, value: unknown, mode?: number): Promise<void> {
  await mkdir(path.dirname(file), { recursive: true });
  const tmp = `${file}.tmp`;
  await writeFile(tmp, JSON.stringify(value, null, 2) + "\n", "utf8");
  if (mode !== undefined) {
    await chmod(tmp, mode).catch(() => undefined);
  }
  await rename(tmp, file);
}

export function exists(file: string): boolean {
  return existsSync(file);
}

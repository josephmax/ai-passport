/**
 * Collector registry: runs every collector with per-collector timeout and
 * error isolation. One failing collector never breaks the snapshot.
 */

import type { Collector, CollectorResult } from "./types.js";

export const COLLECT_TIMEOUT_MS = 15_000;

export async function runCollectors(
  collectors: Collector[],
  now: Date,
  timeoutMs = COLLECT_TIMEOUT_MS,
): Promise<Map<string, CollectorResult>> {
  const results = new Map<string, CollectorResult>();
  await Promise.all(
    collectors.map(async (c) => {
      let result: CollectorResult;
      try {
        result = await Promise.race([
          c.collect(now),
          new Promise<CollectorResult>((_, reject) =>
            setTimeout(() => reject(new Error(`collector ${c.provider} timed out`)), timeoutMs),
          ),
        ]);
      } catch (err) {
        result = {
          provider: c.provider,
          label: c.label,
          ok: false,
          collectedAt: now.toISOString(),
          error: err instanceof Error ? err.message : String(err),
        };
      }
      results.set(c.provider, result);
    }),
  );
  return results;
}

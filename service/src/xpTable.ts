/**
 * XP / level segmentation table — service-side mirror.
 *
 * Mirrors spec `2026-09-22-multi-pendant-app.md` §6.2 (the firmware owns the
 * authoritative implementation; the service keeps this mirror so drift is
 * caught by `test/xpTable.test.ts`):
 *
 *   Level 1-9   : 5  tomatoes to advance
 *   Level 10-29 : 15
 *   Level 30-59 : 25
 *   Level 60-98 : 50
 *   Level 99    : 100 (terminal: reaching 100)
 *
 * Full level 100 = 3145 accumulated tomatoes. Pure integer math only.
 */

export interface XpSegment {
  fromLevel: number;
  toLevel: number;
  costToNext: number;
}

export const XP_SEGMENTS: readonly XpSegment[] = [
  { fromLevel: 1, toLevel: 9, costToNext: 5 },
  { fromLevel: 10, toLevel: 29, costToNext: 15 },
  { fromLevel: 30, toLevel: 59, costToNext: 25 },
  { fromLevel: 60, toLevel: 98, costToNext: 50 },
  { fromLevel: 99, toLevel: 99, costToNext: 100 },
];

export const MAX_LEVEL = 100;

/** Total tomatoes needed to reach max level from level 1 (0 XP). */
export function totalXpToMaxLevel(): number {
  let total = 0;
  for (const seg of XP_SEGMENTS) {
    const levels = seg.toLevel - seg.fromLevel + 1;
    // The terminal segment (99 -> 100) costs its costToNext once.
    total += levels * seg.costToNext;
  }
  return total;
}

/** Cost (in tomatoes) to go from `level` to `level + 1`. */
export function costToNextLevel(level: number): number | null {
  if (level < 1 || level >= MAX_LEVEL) return null;
  for (const seg of XP_SEGMENTS) {
    if (level >= seg.fromLevel && level <= seg.toLevel) return seg.costToNext;
  }
  return null;
}

/**
 * Reverse lookup: level for a given accumulated XP. Integer math only;
 * `xp` below 0 clamps to level 1, above the total clamps to MAX_LEVEL.
 */
export function levelForXp(xp: number): number {
  if (xp <= 0) return 1;
  let remaining = Math.floor(xp);
  for (const seg of XP_SEGMENTS) {
    for (let lvl = seg.fromLevel; lvl <= seg.toLevel; lvl++) {
      if (lvl === MAX_LEVEL) return MAX_LEVEL;
      if (remaining < seg.costToNext) return lvl;
      remaining -= seg.costToNext;
    }
  }
  return MAX_LEVEL;
}

/** XP still needed to advance from the level implied by `xp` to the next level. */
export function xpToNextLevel(xp: number): number {
  const level = levelForXp(xp);
  if (level >= MAX_LEVEL) return 0;
  // xp already spent inside the current level:
  let spentToLevel = 0;
  for (const seg of XP_SEGMENTS) {
    for (let lvl = seg.fromLevel; lvl <= seg.toLevel && lvl < level; lvl++) {
      spentToLevel += seg.costToNext;
    }
  }
  const cost = costToNextLevel(level)!;
  const inLevel = Math.floor(xp) - spentToLevel;
  return cost - inLevel;
}

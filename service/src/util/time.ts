/**
 * Time helpers (pure, host-local timezone).
 *
 * The snapshot `generatedAt` and quota `resetAt` fields use RFC 3339 with the
 * host-local UTC offset (e.g. `2026-09-22T06:00:00+08:00`), matching the spec
 * example in 2026-09-22-multi-pendant-app.md §4.3.
 */

const PAD = (n: number, w = 2): string => String(n).padStart(w, "0");

/** `2026-09-22T06:00:00+08:00` style ISO string in host-local time. */
export function localIsoWithOffset(d: Date): string {
  const offsetMin = -d.getTimezoneOffset();
  const sign = offsetMin >= 0 ? "+" : "-";
  const abs = Math.abs(offsetMin);
  const oh = PAD(Math.floor(abs / 60));
  const om = PAD(abs % 60);
  return (
    `${d.getFullYear()}-${PAD(d.getMonth() + 1)}-${PAD(d.getDate())}` +
    `T${PAD(d.getHours())}:${PAD(d.getMinutes())}:${PAD(d.getSeconds())}` +
    `${sign}${oh}:${om}`
  );
}

/** Local `HH:MM` for sunrise/sunset fields. */
export function localHhMm(d: Date): string {
  return `${PAD(d.getHours())}:${PAD(d.getMinutes())}`;
}

export interface Window {
  start: Date;
  end: Date;
}

/** Current ISO week window: Monday 00:00 local → next Monday 00:00 local. */
export function weekWindow(now: Date): Window {
  const mondayOffset = (now.getDay() + 6) % 7; // Mon=0 .. Sun=6
  const start = new Date(
    now.getFullYear(),
    now.getMonth(),
    now.getDate() - mondayOffset,
    0, 0, 0, 0,
  );
  const end = new Date(start);
  end.setDate(end.getDate() + 7);
  return { start, end };
}

export function addMinutes(d: Date, minutes: number): Date {
  return new Date(d.getTime() + minutes * 60_000);
}

export function minutesBetween(a: Date, b: Date): number {
  return (b.getTime() - a.getTime()) / 60_000;
}

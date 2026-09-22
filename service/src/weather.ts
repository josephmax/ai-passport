/**
 * Weather via open-meteo (key-less): current weather_code + daily
 * sunrise/sunset for the configured location. Cached 30 minutes; on fetch
 * failure the last known values are served (device tolerates staleness via
 * snapshot generatedAt).
 */

import { wmoKind } from "./wmo.js";
import { localIsoWithOffset } from "./util/time.js";

export const WEATHER_CACHE_TTL_MS = 30 * 60_000;

export interface WeatherSnapshot {
  /** Raw WMO code; the device normalizes (mirror also computed here as `kind`). */
  code: number;
  kind: string;
  /** Local "HH:MM" at the queried location (open-meteo timezone=auto). */
  sunrise: string;
  sunset: string;
  city: string;
  fetchedAt: string;
}

interface CacheEntry {
  key: string;
  fetchedAtMs: number;
  snapshot: WeatherSnapshot;
}

export interface WeatherDeps {
  fetchImpl?: typeof fetch;
  now?: () => Date;
}

function hhmmFromIsoLocal(s: unknown): string | null {
  if (typeof s !== "string") return null;
  const m = /T(\d{2}:\d{2})/.exec(s);
  return m && m[1] ? m[1] : null;
}

/** Pure mapping of an open-meteo /v1/forecast response (unit-tested). */
export function mapOpenMeteoResponse(
  json: unknown,
  city: string,
  fetchedAt: string,
): WeatherSnapshot | null {
  if (typeof json !== "object" || json === null) return null;
  const root = json as Record<string, unknown>;
  const current = root.current;
  if (typeof current !== "object" || current === null) return null;
  const code = (current as Record<string, unknown>).weather_code;
  if (typeof code !== "number") return null;
  const daily = root.daily;
  let sunrise: string | null = null;
  let sunset: string | null = null;
  if (typeof daily === "object" && daily !== null) {
    const d = daily as Record<string, unknown>;
    const sr = Array.isArray(d.sunrise) ? d.sunrise[0] : undefined;
    const ss = Array.isArray(d.sunset) ? d.sunset[0] : undefined;
    sunrise = hhmmFromIsoLocal(sr);
    sunset = hhmmFromIsoLocal(ss);
  }
  return {
    code,
    kind: wmoKind(code),
    sunrise: sunrise ?? "06:00",
    sunset: sunset ?? "18:00",
    city,
    fetchedAt,
  };
}

export function buildForecastUrl(lat: number, lon: number): string {
  const u = new URL("https://api.open-meteo.com/v1/forecast");
  u.searchParams.set("latitude", String(lat));
  u.searchParams.set("longitude", String(lon));
  u.searchParams.set("current", "weather_code");
  u.searchParams.set("daily", "sunrise,sunset");
  u.searchParams.set("timezone", "auto");
  u.searchParams.set("forecast_days", "1");
  return u.toString();
}

export class WeatherService {
  private cache: CacheEntry | null = null;
  private readonly fetchImpl: typeof fetch;
  private readonly now: () => Date;

  constructor(deps: WeatherDeps = {}) {
    this.fetchImpl = deps.fetchImpl ?? fetch;
    this.now = deps.now ?? (() => new Date());
  }

  async get(lat: number, lon: number, city: string): Promise<WeatherSnapshot | null> {
    const key = `${lat.toFixed(3)},${lon.toFixed(3)}`;
    const nowMs = this.now().getTime();
    if (this.cache && this.cache.key === key && nowMs - this.cache.fetchedAtMs < WEATHER_CACHE_TTL_MS) {
      return this.cache.snapshot;
    }
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), 10_000);
    try {
      const res = await this.fetchImpl(buildForecastUrl(lat, lon), {
        signal: ctrl.signal,
        headers: { Accept: "application/json" },
      });
      if (!res.ok) throw new Error(`open-meteo HTTP ${res.status}`);
      const json: unknown = await res.json();
      const snapshot = mapOpenMeteoResponse(json, city, localIsoWithOffset(this.now()));
      if (!snapshot) throw new Error("unrecognized open-meteo response");
      this.cache = { key, fetchedAtMs: nowMs, snapshot };
      return snapshot;
    } catch {
      // serve last known even if for another key/location: better than nothing
      return this.cache ? this.cache.snapshot : null;
    } finally {
      clearTimeout(timer);
    }
  }

  /** For tests / smoke checks. */
  cached(): WeatherSnapshot | null {
    return this.cache ? this.cache.snapshot : null;
  }
}

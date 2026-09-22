/**
 * WMO weather-code normalization.
 *
 * Byte-for-byte mirror of the firmware implementation in
 * `main/app/app_weather.c` (`app_weather_from_wmo`). Any change here MUST be
 * mirrored in firmware and vice versa; `test/wmo.test.ts` validates the full
 * 0..99 code range against the same rule table.
 *
 * Rule (spec 2026-09-22-multi-pendant-app.md §5.2):
 *   0-2          -> sunny
 *   3, 45, 48    -> cloudy
 *   51-67, 80-82 -> rain
 *   71-77, 85-86 -> snow
 *   anything else -> cloudy (unknown codes degrade conservatively)
 */

export type WeatherKind = "sunny" | "cloudy" | "rain" | "snow";

export function wmoKind(code: number): WeatherKind {
  if (code >= 0 && code <= 2) return "sunny";
  if (code === 3 || code === 45 || code === 48) return "cloudy";
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return "rain";
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return "snow";
  return "cloudy";
}

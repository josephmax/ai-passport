/**
 * Built-in city table for weather lookup (P1: ~30 common Chinese cities).
 * Users can also set a custom latitude/longitude pair in the portal
 * preferences page, which overrides the city selection.
 */

export interface CityEntry {
  id: string;
  name: string; // Chinese display name (snapshot `weather.city`)
  lat: number;
  lon: number;
}

export const CITY_TABLE: readonly CityEntry[] = [
  { id: "beijing", name: "北京", lat: 39.9042, lon: 116.4074 },
  { id: "shanghai", name: "上海", lat: 31.2304, lon: 121.4737 },
  { id: "guangzhou", name: "广州", lat: 23.1291, lon: 113.2644 },
  { id: "shenzhen", name: "深圳", lat: 22.5431, lon: 114.0579 },
  { id: "hangzhou", name: "杭州", lat: 30.2741, lon: 120.1551 },
  { id: "chengdu", name: "成都", lat: 30.5728, lon: 104.0668 },
  { id: "chongqing", name: "重庆", lat: 29.563, lon: 106.5516 },
  { id: "wuhan", name: "武汉", lat: 30.5928, lon: 114.3055 },
  { id: "xian", name: "西安", lat: 34.3416, lon: 108.9398 },
  { id: "nanjing", name: "南京", lat: 32.0603, lon: 118.7969 },
  { id: "tianjin", name: "天津", lat: 39.3434, lon: 117.3616 },
  { id: "suzhou", name: "苏州", lat: 31.2989, lon: 120.5853 },
  { id: "zhengzhou", name: "郑州", lat: 34.7466, lon: 113.6254 },
  { id: "changsha", name: "长沙", lat: 28.2282, lon: 112.9388 },
  { id: "shenyang", name: "沈阳", lat: 41.8057, lon: 123.4315 },
  { id: "qingdao", name: "青岛", lat: 36.0671, lon: 120.3826 },
  { id: "dalian", name: "大连", lat: 38.914, lon: 121.6147 },
  { id: "xiamen", name: "厦门", lat: 24.4798, lon: 118.0894 },
  { id: "hefei", name: "合肥", lat: 31.8206, lon: 117.2272 },
  { id: "fuzhou", name: "福州", lat: 26.0745, lon: 119.2965 },
  { id: "kunming", name: "昆明", lat: 24.8801, lon: 102.8329 },
  { id: "guiyang", name: "贵阳", lat: 26.6477, lon: 106.6302 },
  { id: "nanning", name: "南宁", lat: 22.817, lon: 108.3665 },
  { id: "haikou", name: "海口", lat: 20.0444, lon: 110.1999 },
  { id: "harbin", name: "哈尔滨", lat: 45.8038, lon: 126.535 },
  { id: "changchun", name: "长春", lat: 43.8171, lon: 125.3235 },
  { id: "shijiazhuang", name: "石家庄", lat: 38.0428, lon: 114.5149 },
  { id: "taiyuan", name: "太原", lat: 37.8706, lon: 112.5489 },
  { id: "nanchang", name: "南昌", lat: 28.682, lon: 115.8579 },
  { id: "jinan", name: "济南", lat: 36.6512, lon: 117.1201 },
  { id: "lanzhou", name: "兰州", lat: 36.0611, lon: 103.8343 },
  { id: "urumqi", name: "乌鲁木齐", lat: 43.8256, lon: 87.6168 },
  { id: "hohhot", name: "呼和浩特", lat: 40.8414, lon: 111.7519 },
];

export function findCity(id: string): CityEntry | undefined {
  return CITY_TABLE.find((c) => c.id === id);
}

/** Resolve the effective weather location from settings. */
export function resolveLocation(
  cityId: string | null,
  customLat: number | null,
  customLon: number | null,
): { lat: number; lon: number; name: string } {
  if (customLat !== null && customLon !== null) {
    return { lat: customLat, lon: customLon, name: "自定义" };
  }
  const city = (cityId && findCity(cityId)) || CITY_TABLE[0]!;
  return { lat: city.lat, lon: city.lon, name: city.name };
}

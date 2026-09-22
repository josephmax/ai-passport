/**
 * PNG -> RGB565 conversion pipeline (server-side transcode, ADR-0004).
 * sharp decodes; pixels are packed to little-endian u16 RGB565 frames.
 */

import sharp from "sharp";
import { rawToRgb565A8Le, rawToRgb565Le } from "./rgb565.js";

export interface ConvertedImage {
  w: number;
  h: number;
  /** RGB565 (map) = w*h*2; RGB565A8 (sprites) = w*h*3 (color plane + A8). */
  data: Buffer;
}

export class AssetValidationError extends Error {}

/**
 * Convert one PNG buffer to a device frame. Sprites keep PNG alpha as an A8
 * plane (RGB565A8); the map uses opaque RGB565. Throws AssetValidationError
 * when the buffer is not a PNG.
 */
export async function convertPngToRgb565(
  buf: Buffer,
  opts: { alpha: boolean } = { alpha: true },
): Promise<ConvertedImage> {
  let meta: sharp.Metadata;
  try {
    meta = await sharp(buf).metadata();
  } catch (err) {
    throw new AssetValidationError(`无法解码图片: ${err instanceof Error ? err.message : err}`);
  }
  if (meta.format !== "png") {
    throw new AssetValidationError(`仅支持 PNG（收到 ${meta.format ?? "未知格式"}）`);
  }
  const pipeline = opts.alpha ? sharp(buf).ensureAlpha() : sharp(buf).removeAlpha();
  const { data, info } = await pipeline.raw().toBuffer({ resolveWithObject: true });
  if (info.channels !== 3 && info.channels !== 4) {
    throw new AssetValidationError(`不支持的通道数: ${info.channels}`);
  }
  const bmp = {
    width: info.width,
    height: info.height,
    data,
    channels: info.channels as 3 | 4,
  };
  return {
    w: info.width,
    h: info.height,
    data: opts.alpha ? rawToRgb565A8Le(bmp) : rawToRgb565Le(bmp),
  };
}

/** Validate exact dimensions for a slot. */
export async function convertWithSize(
  buf: Buffer,
  expectedW: number,
  expectedH: number,
  what: string,
  opts: { alpha: boolean } = { alpha: true },
): Promise<ConvertedImage> {
  const img = await convertPngToRgb565(buf, opts);
  if (img.w !== expectedW || img.h !== expectedH) {
    throw new AssetValidationError(
      `${what} 尺寸必须为 ${expectedW}×${expectedH}，实际 ${img.w}×${img.h}`,
    );
  }
  return img;
}

// Frame-count rules (spec §5.1 and portal tasking).
export const ACTION_RULES = {
  run: { w: 64, h: 64, minFrames: 4, maxFrames: 8 },
  fight: { w: 64, h: 64, minFrames: 4, maxFrames: 8 },
  sleep: { w: 64, h: 64, minFrames: 2, maxFrames: 4 },
  victory: { w: 64, h: 64, minFrames: 1, maxFrames: 4 },
} as const;

export type ActionName = keyof typeof ACTION_RULES;

export function validateActionFrameCount(
  slot: ActionName,
  frames: number,
): void {
  const r = ACTION_RULES[slot];
  if (frames < r.minFrames || frames > r.maxFrames) {
    throw new AssetValidationError(
      `${slot} 动作帧数必须为 ${r.minFrames}–${r.maxFrames} 帧，收到 ${frames} 帧`,
    );
  }
}

export function validateFps(fps: number): number {
  const n = Math.round(fps);
  if (!Number.isFinite(n) || n < 1 || n > 10) {
    throw new AssetValidationError("帧率必须为 1–10 fps");
  }
  return n;
}

export const MAP_W = 240;
export const MAP_H = 160;
export const DECO_W = 24;
export const DECO_H = 24;
export const MAX_DECORATIONS = 8;
export const MAX_UPLOAD_FILES = 12;

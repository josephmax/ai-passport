/**
 * RGB565 conversion (pure).
 *
 * Frame data files are per-frame RGB565 pixels, concatenated in frame order.
 * Each pixel is one u16 in **little-endian** byte order (LVGL native), so a
 * pixel with value 0xF800 (pure red) serializes as bytes `00 F8`.
 * Frame size = w * h * 2 bytes.
 */

export function rgbTo565(r: number, g: number, b: number): number {
  const r5 = (r >> 3) & 0x1f;
  const g6 = (g >> 2) & 0x3f;
  const b5 = (b >> 3) & 0x1f;
  return (r5 << 11) | (g6 << 5) | b5;
}

export interface RawBitmap {
  width: number;
  height: number;
  /** Raw decoded pixels from sharp (`.raw()`), 3 or 4 channels. */
  data: Buffer | Uint8Array;
  channels: 3 | 4;
}

/** Convert a raw RGBA/RGB bitmap to a little-endian RGB565 frame buffer. */
export function rawToRgb565Le(bmp: RawBitmap): Buffer {
  const out = Buffer.alloc(bmp.width * bmp.height * 2);
  const px = bmp.width * bmp.height;
  const step = bmp.channels;
  for (let i = 0; i < px; i++) {
    const r = bmp.data[i * step]!;
    const g = bmp.data[i * step + 1]!;
    const b = bmp.data[i * step + 2]!;
    const v = rgbTo565(r, g, b);
    // little-endian u16
    out[i * 2] = v & 0xff;
    out[i * 2 + 1] = (v >> 8) & 0xff;
  }
  return out;
}

/** Helper for procedural (non-sharp) frame generation. */
export class Rgb565Canvas {
  readonly width: number;
  readonly height: number;
  private readonly buf: Buffer;

  constructor(width: number, height: number) {
    this.width = width;
    this.height = height;
    this.buf = Buffer.alloc(width * height * 2);
  }

  set(x: number, y: number, r: number, g: number, b: number): void {
    const xi = Math.round(x);
    const yi = Math.round(y);
    if (xi < 0 || yi < 0 || xi >= this.width || yi >= this.height) return;
    const v = rgbTo565(r, g, b);
    const i = (yi * this.width + xi) * 2;
    this.buf[i] = v & 0xff;
    this.buf[i + 1] = (v >> 8) & 0xff;
  }

  fillRect(x: number, y: number, w: number, h: number, r: number, g: number, b: number): void {
    for (let dy = 0; dy < h; dy++) {
      for (let dx = 0; dx < w; dx++) this.set(x + dx, y + dy, r, g, b);
    }
  }

  /** Bresenham-ish thick line, clipped. Coordinates are rounded to integers. */
  line(x0: number, y0: number, x1: number, y1: number, r: number, g: number, b: number, thickness = 1): void {
    let x = Math.round(x0);
    let y = Math.round(y0);
    const ex = Math.round(x1);
    const ey = Math.round(y1);
    const dx = Math.abs(ex - x);
    const dy = Math.abs(ey - y);
    const sx = x < ex ? 1 : -1;
    const sy = y < ey ? 1 : -1;
    let err = dx - dy;
    // integer deltas guarantee termination, but keep a hard cap as defense
    let budget = dx + dy + 2;
    for (;;) {
      const half = Math.floor((thickness - 1) / 2);
      this.fillRect(x - half, y - half, thickness, thickness, r, g, b);
      if ((x === ex && y === ey) || budget-- <= 0) break;
      const e2 = 2 * err;
      if (e2 > -dy) { err -= dy; x += sx; }
      if (e2 < dx) { err += dx; y += sy; }
    }
  }

  circle(cx: number, cy: number, radius: number, r: number, g: number, b: number, fill = true): void {
    for (let y = -radius; y <= radius; y++) {
      for (let x = -radius; x <= radius; x++) {
        const d2 = x * x + y * y;
        if (fill ? d2 <= radius * radius : Math.abs(d2 - radius * radius) <= radius) {
          this.set(cx + x, cy + y, r, g, b);
        }
      }
    }
  }

  toBuffer(): Buffer {
    return Buffer.from(this.buf);
  }
}

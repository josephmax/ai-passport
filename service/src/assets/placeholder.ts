/**
 * Default placeholder asset bundle v1 generator (pure code, no image files).
 *
 * Generates simple pixel art so a fresh service always has a publishable
 * bundle_v1.bin for out-of-the-box devices:
 *   - 4 actions (running figure / sword fight vs a tomato / moon sleep /
 *     trophy victory), 64x64 per spec frame counts
 *   - 240x160 horizontally-seamless map strip (vertical gradient sky,
 *     periodic mountains/grass)
 *   - a 24x24 bush decoration, 16x16 rain and snow sprites
 *
 * The manifest matches the spec example exactly.
 */

import { Rgb565Canvas } from "./rgb565.js";
import {
  packBundle,
  type AssetManifest,
  type BundleFile,
} from "./bundleFormat.js";

const TAU = Math.PI * 2;

// --- 64x64 action sprites ---------------------------------------------------

function drawStickFigure(c: Rgb565Canvas, cx: number, cy: number, phase: number, bob: number): void {
  const y0 = cy + bob;
  // shadow
  c.fillRect(cx - 10, 57, 20, 2, 40, 40, 48);
  // head
  c.circle(cx, y0 - 44, 5, 250, 220, 185);
  // body
  c.line(cx, y0 - 39, cx, y0 - 22, 240, 240, 245, 2);
  // arms swing opposite legs
  const arm = Math.sin(phase) * 8;
  c.line(cx, y0 - 35, cx - arm, y0 - 27, 240, 240, 245, 2);
  c.line(cx, y0 - 35, cx + arm, y0 - 27, 240, 240, 245, 2);
  // legs
  const leg = Math.sin(phase) * 10;
  c.line(cx, y0 - 22, cx - leg, y0 - 8, 240, 240, 245, 2);
  c.line(cx, y0 - 22, cx + leg, y0 - 8, 240, 240, 245, 2);
}

export function generateRunFrames(): Buffer[] {
  const frames: Buffer[] = [];
  for (let i = 0; i < 6; i++) {
    const c = new Rgb565Canvas(64, 64);
    const phase = (i / 6) * TAU;
    drawStickFigure(c, 28, 60, phase, i % 2 === 0 ? 0 : -1);
    // speed dashes behind the runner
    for (let k = 0; k < 3; k++) {
      c.line(10 + k * 4, 34 + k * 3, 18 + k * 4, 34 + k * 3, 90, 160, 255, 1);
    }
    frames.push(c.toBuffer());
  }
  return frames;
}

export function generateFightFrames(): Buffer[] {
  const frames: Buffer[] = [];
  for (let i = 0; i < 6; i++) {
    const c = new Rgb565Canvas(64, 64);
    const y0 = 60;
    // ground shadow
    c.fillRect(8, 57, 48, 2, 40, 40, 48);
    // stick fighter, static stance
    c.circle(24, y0 - 44, 5, 250, 220, 185);
    c.line(24, y0 - 39, 24, y0 - 22, 240, 240, 245, 2);
    c.line(24, y0 - 35, 18, y0 - 26, 240, 240, 245, 2); // off hand
    c.line(24, y0 - 22, 18, y0 - 8, 240, 240, 245, 2);
    c.line(24, y0 - 22, 30, y0 - 8, 240, 240, 245, 2);
    // tomato "monster" wobbles
    const wob = Math.sin((i / 6) * TAU) * 2;
    c.circle(48, y0 - 18 + wob, 8, 220, 60, 50);
    c.fillRect(46, y0 - 28 + wob, 4, 4, 60, 160, 60); // stem
    c.circle(45, y0 - 20 + wob, 1, 250, 250, 250);
    c.circle(51, y0 - 20 + wob, 1, 250, 250, 250);
    // sword: frames 0-2 windup (up), 3-5 swing (down-forward) with slash arc
    const windup = i < 3;
    const hand = { x: 30, y: y0 - 34 };
    const tip = windup
      ? { x: hand.x + 6, y: hand.y - 18 }
      : { x: hand.x + 16, y: hand.y + Math.floor(i * 3) };
    c.line(hand.x, hand.y, tip.x, tip.y, 200, 205, 215, 2);
    c.circle(hand.x, hand.y, 2, 250, 220, 185);
    if (!windup) {
      // slash arc pixels
      for (let k = 0; k < 6; k++) {
        c.set(40 + k, y0 - 30 + k * 2, 255, 255, 140);
      }
    }
    frames.push(c.toBuffer());
  }
  return frames;
}

export function generateSleepFrames(): Buffer[] {
  const frames: Buffer[] = [];
  for (let i = 0; i < 2; i++) {
    const c = new Rgb565Canvas(64, 64);
    // crescent moon
    c.circle(48, 13, 9, 250, 230, 140);
    c.circle(53, 10, 8, 0, 0, 0);
    // stars
    c.set(8, 10, 250, 250, 250);
    c.set(20, 20, 250, 250, 250);
    c.set(38, 6, 250, 250, 250);
    c.set(14, 34, 250, 250, 250);
    // sleeping figure lying on the ground
    c.fillRect(4, 50, 56, 4, 40, 40, 48); // ground line
    c.circle(14, 45, 5, 250, 220, 185); // head
    c.line(20, 46, 44, 46, 240, 240, 245, 3); // body
    c.line(44, 46, 52, 42, 240, 240, 245, 2); // legs up a bit
    // Z glyphs drift up with frame index
    const zs = i === 0 ? 2 : 3;
    for (let z = 0; z < zs; z++) {
      const zx = 36 + z * 7;
      const zy = 38 - z * 8 - (i === 1 ? 2 : 0);
      c.line(zx, zy, zx + 5, zy, 140, 200, 255, 1);
      c.line(zx + 5, zy, zx, zy + 5, 140, 200, 255, 1);
      c.line(zx, zy + 5, zx + 5, zy + 5, 140, 200, 255, 1);
    }
    frames.push(c.toBuffer());
  }
  return frames;
}

export function generateVictoryFrames(): Buffer[] {
  const frames: Buffer[] = [];
  for (let i = 0; i < 4; i++) {
    const c = new Rgb565Canvas(64, 64);
    const bob = Math.sin((i / 4) * TAU) * 2;
    const y = 30 + bob;
    // confetti
    const colors: [number, number, number][] = [
      [255, 90, 90], [90, 200, 255], [255, 220, 90], [140, 255, 140],
    ];
    for (let k = 0; k < 10; k++) {
      const x = (k * 13 + i * 5) % 64;
      const yy = (k * 7 + i * 9) % 64;
      const col = colors[k % colors.length]!;
      c.set(x, yy, col[0], col[1], col[2]);
      c.set(x + 1, yy, col[0], col[1], col[2]);
    }
    // trophy
    c.fillRect(24, y - 6, 16, 10, 255, 200, 40); // bowl
    c.fillRect(22, y - 6, 20, 2, 230, 170, 30); // rim
    c.line(22, y - 4, 18, y + 2, 255, 200, 40, 2); // handles
    c.line(42, y - 4, 46, y + 2, 255, 200, 40, 2);
    c.fillRect(30, y + 4, 4, 5, 230, 170, 30); // stem
    c.fillRect(26, y + 9, 12, 3, 230, 170, 30); // base
    // shine
    c.set(28, y - 3, 255, 255, 255);
    c.set(29, y - 2, 255, 255, 255);
    frames.push(c.toBuffer());
  }
  return frames;
}

// --- map strip ---------------------------------------------------------------

export function generateMapStrip(): Buffer {
  const W = 240;
  const H = 160;
  const c = new Rgb565Canvas(W, H);
  const lerp = (a: number, b: number, t: number) => Math.round(a + (b - a) * t);
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      if (y < 96) {
        // sky gradient, seamlessly periodic in x (constant in x anyway)
        const t = y / 96;
        c.set(x, y, lerp(96, 190, t), lerp(150, 225, t), lerp(255, 255, t));
      } else if (y < 108) {
        // distant mountains: two periodic octaves, period 240 -> seamless loop
        const m1 = Math.abs(Math.sin((x / W) * TAU * 2)) * 8;
        const m2 = Math.abs(Math.sin((x / W) * TAU * 3 + 1.3)) * 5;
        const ridge = 108 - Math.max(6, m1 + m2);
        if (y > ridge) c.set(x, y, 108, 128, 168);
        else c.set(x, y, lerp(190, 200, (y - 96) / 12), 225, 252);
      } else {
        // grass with subtle periodic shading + a path band
        const band = Math.sin((x / W) * TAU * 6) > 0.4 ? 6 : 0;
        if (y >= 124 && y < 138) {
          // dirt path with periodic dashes
          const dash = (x % 30) < 20 ? 0 : 12;
          c.set(x, y, 205 + dash, 175 + dash, 125 + dash);
        } else {
          c.set(x, y, 56 + band, 158 + band, 66 + band / 2);
        }
      }
    }
  }
  // sun
  c.circle(48, 24, 9, 255, 240, 160);
  // flowers on the grass (fixed positions, part of the looping texture)
  const flowers: [number, number][] = [
    [20, 118], [70, 150], [120, 116], [170, 146], [220, 120], [200, 155],
  ];
  for (const [fx, fy] of flowers) {
    c.set(fx, fy, 255, 120, 140);
    c.set(fx + 1, fy, 255, 120, 140);
    c.set(fx, fy + 1, 255, 120, 140);
    c.set(fx + 1, fy + 1, 255, 120, 140);
  }
  return c.toBuffer();
}

// --- decoration & weather sprites --------------------------------------------

export function generateDecoration(): Buffer {
  const c = new Rgb565Canvas(24, 24);
  c.circle(8, 16, 6, 46, 130, 52);
  c.circle(15, 14, 7, 60, 150, 64);
  c.circle(20, 17, 5, 46, 130, 52);
  c.fillRect(6, 20, 14, 3, 30, 90, 36);
  c.set(13, 10, 255, 120, 140); // little berry
  return c.toBuffer();
}

export function generateRainFrames(): Buffer[] {
  const frames: Buffer[] = [];
  for (let f = 0; f < 2; f++) {
    const c = new Rgb565Canvas(16, 16);
    for (let k = 0; k < 5; k++) {
      const x = (k * 5 + f * 3) % 16;
      const y = (k * 3 + f * 4) % 16;
      c.set(x, y, 120, 170, 255);
      c.set(x - 1, y + 1, 90, 140, 240);
      c.set(x - 2, y + 2, 60, 110, 220);
    }
    frames.push(c.toBuffer());
  }
  return frames;
}

export function generateSnowFrames(): Buffer[] {
  const frames: Buffer[] = [];
  for (let f = 0; f < 2; f++) {
    const c = new Rgb565Canvas(16, 16);
    for (let k = 0; k < 6; k++) {
      const x = (k * 7 + f * 2) % 16;
      const y = (k * 5 + f * 5) % 16;
      c.set(x, y, 250, 252, 255);
      c.set(x + 1, y, 210, 220, 240);
      c.set(x, y + 1, 210, 220, 240);
    }
    frames.push(c.toBuffer());
  }
  return frames;
}

// --- bundle assembly ----------------------------------------------------------

export const ACTION_SPECS = {
  run: { frames: 6, fps: 6, w: 64, h: 64 },
  fight: { frames: 6, fps: 8, w: 64, h: 64 },
  sleep: { frames: 2, fps: 2, w: 64, h: 64 },
  victory: { frames: 4, fps: 6, w: 64, h: 64 },
} as const;

export type ActionSlot = keyof typeof ACTION_SPECS;

function concatFrames(frames: Buffer[]): Buffer {
  return Buffer.concat(frames);
}

/** Generated default files (action/map/deco/weather), names canonical. */
export function generatePlaceholderDataFiles(): Map<string, Buffer> {
  const files = new Map<string, Buffer>();
  files.set("run.bin", concatFrames(generateRunFrames()));
  files.set("fight.bin", concatFrames(generateFightFrames()));
  files.set("sleep.bin", concatFrames(generateSleepFrames()));
  files.set("victory.bin", concatFrames(generateVictoryFrames()));
  files.set("map.bin", generateMapStrip());
  files.set("deco0.bin", generateDecoration());
  files.set("rain.bin", concatFrames(generateRainFrames()));
  files.set("snow.bin", concatFrames(generateSnowFrames()));
  return files;
}

export function placeholderManifest(version: number): AssetManifest {
  return {
    version,
    actions: {
      run: { file: "run.bin", frames: 6, fps: 6, w: 64, h: 64 },
      fight: { file: "fight.bin", frames: 6, fps: 8, w: 64, h: 64 },
      sleep: { file: "sleep.bin", frames: 2, fps: 2, w: 64, h: 64 },
      victory: { file: "victory.bin", frames: 4, fps: 6, w: 64, h: 64 },
    },
    map: { file: "map.bin", w: 240, h: 160 },
    decorations: [{ file: "deco0.bin", w: 24, h: 24 }],
    weather: {
      rain: { file: "rain.bin", frames: 2, fps: 6, w: 16, h: 16 },
      snow: { file: "snow.bin", frames: 2, fps: 4, w: 16, h: 16 },
    },
  };
}

/** Full placeholder bundle (packaged), e.g. for tools/gen-placeholder-bundle.ts. */
export function generatePlaceholderBundle(version = 1): Buffer {
  const files: BundleFile[] = [
    { name: "manifest.json", data: Buffer.from(JSON.stringify(placeholderManifest(version), null, 2), "utf8") },
    ...[...generatePlaceholderDataFiles().entries()].map(([name, data]) => ({ name, data })),
  ];
  return packBundle(version, files);
}

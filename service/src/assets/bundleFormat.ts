/**
 * APB1 asset-bundle container format (byte-exact contract with the firmware).
 *
 * ```
 * offset  len  field
 * 0x00    4    magic "APB1" (0x41 0x50 0x42 0x31)
 * 0x04    2    version (u16 LE)
 * 0x06    2    file_count (u16 LE)
 * then file_count entries:
 *   u16 name_len (LE) + name_len bytes UTF-8 relative path (no leading slash)
 *   u32 data_len (LE) + data_len bytes raw content
 * ```
 * All multi-byte integers little-endian. Hard limits: 65535 files, u32 sizes.
 * The publisher additionally enforces a 4 MB total bundle cap (LittleFS budget).
 */

export const BUNDLE_MAGIC = Buffer.from([0x41, 0x50, 0x42, 0x31]); // "APB1"
export const BUNDLE_MAX_BYTES = 4 * 1024 * 1024; // 4 MB

export interface BundleFile {
  name: string;
  data: Buffer;
}

export interface ParsedBundle {
  version: number;
  files: BundleFile[];
}

export class BundleFormatError extends Error {}

export function packBundle(version: number, files: BundleFile[]): Buffer {
  if (!Number.isInteger(version) || version < 1 || version > 0xffff) {
    throw new BundleFormatError(`bundle version must be 1..65535, got ${version}`);
  }
  if (files.length < 1 || files.length > 0xffff) {
    throw new BundleFormatError(`file_count must be 1..65535, got ${files.length}`);
  }
  const parts: Buffer[] = [BUNDLE_MAGIC];
  const header = Buffer.alloc(4);
  header.writeUInt16LE(version, 0);
  header.writeUInt16LE(files.length, 2);
  parts.push(header);
  for (const f of files) {
    const nameBuf = Buffer.from(f.name, "utf8");
    if (nameBuf.length < 1 || nameBuf.length > 0xffff) {
      throw new BundleFormatError(`bad file name length for "${f.name}"`);
    }
    if (f.name.startsWith("/") || f.name.includes("\\") || f.name.split("/").includes("..")) {
      throw new BundleFormatError(`file name must be a safe relative path: "${f.name}"`);
    }
    if (!Buffer.isBuffer(f.data) || f.data.length > 0xffff_ffff) {
      throw new BundleFormatError(`bad data for "${f.name}"`);
    }
    const nh = Buffer.alloc(2);
    nh.writeUInt16LE(nameBuf.length, 0);
    const dh = Buffer.alloc(4);
    dh.writeUInt32LE(f.data.length, 0);
    parts.push(nh, nameBuf, dh, f.data);
  }
  const total = parts.reduce((n, p) => n + p.length, 0);
  if (total > BUNDLE_MAX_BYTES) {
    throw new BundleFormatError(
      `bundle too large: ${total} bytes > ${BUNDLE_MAX_BYTES} (4 MB cap)`,
    );
  }
  return Buffer.concat(parts, total);
}

export function parseBundle(buf: Buffer): ParsedBundle {
  if (buf.length < 8) throw new BundleFormatError("buffer too short for APB1 header");
  if (!BUNDLE_MAGIC.equals(buf.subarray(0, 4))) {
    throw new BundleFormatError("bad magic, expected APB1");
  }
  const version = buf.readUInt16LE(4);
  const fileCount = buf.readUInt16LE(6);
  const files: BundleFile[] = [];
  let off = 8;
  for (let i = 0; i < fileCount; i++) {
    if (off + 2 > buf.length) throw new BundleFormatError(`truncated name_len at file ${i}`);
    const nameLen = buf.readUInt16LE(off);
    off += 2;
    if (off + nameLen + 4 > buf.length) throw new BundleFormatError(`truncated name/data at file ${i}`);
    const name = buf.subarray(off, off + nameLen).toString("utf8");
    off += nameLen;
    const dataLen = buf.readUInt32LE(off);
    off += 4;
    if (off + dataLen > buf.length) throw new BundleFormatError(`truncated data at file ${i}`);
    files.push({ name, data: Buffer.from(buf.subarray(off, off + dataLen)) });
    off += dataLen;
  }
  if (off !== buf.length) throw new BundleFormatError("trailing bytes after last file");
  return { version, files };
}

// ---------------------------------------------------------------------------
// manifest.json shape (mirrors the spec example exactly; extra keys allowed)
// ---------------------------------------------------------------------------

export interface ManifestAction {
  file: string;
  frames: number;
  fps: number;
  w: number;
  h: number;
}

export interface ManifestMap {
  file: string;
  w: number;
  h: number;
}

export interface ManifestDecoration {
  file: string;
  w: number;
  h: number;
}

export interface ManifestWeatherEffect {
  file: string;
  frames: number;
  fps: number;
  w: number;
  h: number;
}

export interface AssetManifest {
  version: number;
  actions: {
    run: ManifestAction;
    fight: ManifestAction;
    sleep: ManifestAction;
    victory: ManifestAction;
  };
  map: ManifestMap;
  decorations: ManifestDecoration[];
  weather: {
    rain: ManifestWeatherEffect;
    snow: ManifestWeatherEffect;
  };
}

export const MANIFEST_NAME = "manifest.json";

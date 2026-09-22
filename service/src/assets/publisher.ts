/**
 * Skin bundle publisher: draft -> published versioned bundles with history
 * and rollback (rollback = republish an old version's files under a NEW
 * version number, per spec). The first publish ever is the auto-generated
 * placeholder bundle v1.
 */

import path from "node:path";
import { readFile, writeFile, mkdir } from "node:fs/promises";
import { existsSync, readFileSync } from "node:fs";
import {
  packBundle,
  parseBundle,
  MANIFEST_NAME,
  type AssetManifest,
  type BundleFile,
} from "./bundleFormat.js";
import {
  generatePlaceholderDataFiles,
  placeholderManifest,
  ACTION_SPECS,
  type ActionSlot,
} from "./placeholder.js";
import type { Draft } from "./draftStore.js";

export interface PublishedEntry {
  version: number;
  publishedAt: string;
  bytes: number;
  source: string;
}

export interface PublishedMeta {
  current: number;
  history: PublishedEntry[];
}

const EMPTY_META: PublishedMeta = { current: 0, history: [] };

export class AssetPublisher {
  private readonly dir: string;
  private meta: PublishedMeta;

  constructor(assetsDir: string) {
    this.dir = assetsDir;
    this.meta = readMeta(path.join(assetsDir, "published.json"));
  }

  currentVersion(): number {
    return this.meta.current;
  }

  history(): PublishedEntry[] {
    return [...this.meta.history].sort((a, b) => b.version - a.version);
  }

  bundlePath(version: number): string {
    return path.join(this.dir, `bundle_v${version}.bin`);
  }

  async readBundle(version: number): Promise<Buffer | null> {
    const p = this.bundlePath(version);
    if (!existsSync(p)) return null;
    return readFile(p);
  }

  private nextVersion(): number {
    return this.meta.history.reduce((m, h) => Math.max(m, h.version), 0) + 1;
  }

  /** Pack + persist a file set as a new version. */
  async publishFiles(files: BundleFile[], source: string): Promise<PublishedEntry> {
    const version = this.nextVersion();
    const manifest = files.find((f) => f.name === MANIFEST_NAME);
    if (!manifest) throw new Error("bundle file set must contain manifest.json");
    const parsedManifest = JSON.parse(manifest.data.toString("utf8")) as AssetManifest;
    if (parsedManifest.version !== version) {
      throw new Error(
        `manifest version ${parsedManifest.version} does not match next bundle version ${version}`,
      );
    }
    const buf = packBundle(version, files); // throws on >4MB
    await mkdir(this.dir, { recursive: true });
    await writeFile(this.bundlePath(version), buf);
    const entry: PublishedEntry = {
      version,
      publishedAt: new Date().toISOString(),
      bytes: buf.length,
      source,
    };
    this.meta.history.push(entry);
    this.meta.current = version;
    await writeFile(
      path.join(this.dir, "published.json"),
      JSON.stringify(this.meta, null, 2) + "\n",
      "utf8",
    );
    return entry;
  }

  /** Build a full file set from the draft; missing slots fall back to placeholder art. */
  async buildFilesFromDraft(draft: Draft, version: number): Promise<BundleFile[]> {
    const placeholder = generatePlaceholderDataFiles();
    const files = new Map<string, Buffer>();
    const actions: AssetManifest["actions"] = {} as AssetManifest["actions"];

    for (const slot of Object.keys(ACTION_SPECS) as ActionSlot[]) {
      const d = draft.actions[slot];
      if (d && d.frames === d.files.length && d.frames > 0) {
        const parts: Buffer[] = [];
        for (const f of d.files) parts.push(await this.readDraft(f));
        files.set(`${slot}.bin`, Buffer.concat(parts));
        actions[slot] = { file: `${slot}.bin`, frames: d.frames, fps: d.fps, w: d.w, h: d.h };
      } else {
        files.set(`${slot}.bin`, placeholder.get(`${slot}.bin`)!);
        const spec = ACTION_SPECS[slot];
        actions[slot] = { file: `${slot}.bin`, frames: spec.frames, fps: spec.fps, w: spec.w, h: spec.h };
      }
    }

    if (draft.map) {
      files.set("map.bin", await this.readDraft(draft.map.file));
    } else {
      files.set("map.bin", placeholder.get("map.bin")!);
    }

    const decorations: AssetManifest["decorations"] = [];
    if (draft.decorations.length > 0) {
      for (let i = 0; i < draft.decorations.length; i++) {
        const d = draft.decorations[i]!;
        const name = `deco${i}.bin`;
        files.set(name, await this.readDraft(d.file));
        decorations.push({ file: name, w: d.w, h: d.h });
      }
    } else {
      files.set("deco0.bin", placeholder.get("deco0.bin")!);
      decorations.push({ file: "deco0.bin", w: 24, h: 24 });
    }

    // Weather sprites: P1 always uses the built-in generated set (no upload UI).
    files.set("rain.bin", placeholder.get("rain.bin")!);
    files.set("snow.bin", placeholder.get("snow.bin")!);

    const manifest: AssetManifest = {
      version,
      actions,
      map: draft.map
        ? { file: "map.bin", w: draft.map.w, h: draft.map.h }
        : { file: "map.bin", w: 240, h: 160 },
      decorations,
      weather: {
        rain: { file: "rain.bin", frames: 2, fps: 6, w: 16, h: 16 },
        snow: { file: "snow.bin", frames: 2, fps: 4, w: 16, h: 16 },
      },
    };

    const list: BundleFile[] = [
      { name: MANIFEST_NAME, data: Buffer.from(JSON.stringify(manifest, null, 2), "utf8") },
      ...[...files.entries()].map(([name, data]) => ({ name, data })),
    ];
    return list;
  }

  private async readDraft(file: string): Promise<Buffer> {
    // DraftStore owns the draft dir; publisher receives the store path base.
    return readFile(path.join(this.dir, "draft", file));
  }

  async publishFromDraft(draft: Draft, source = "draft"): Promise<PublishedEntry> {
    const version = this.nextVersion();
    const files = await this.buildFilesFromDraft(draft, version);
    return this.publishFiles(files, source);
  }

  /** Republish an existing version's files under a new version number. */
  async rollback(toVersion: number): Promise<PublishedEntry> {
    const buf = await this.readBundle(toVersion);
    if (!buf) throw new Error(`版本 v${toVersion} 不存在`);
    const parsed = parseBundle(buf);
    const version = this.nextVersion();
    const manifest = JSON.parse(
      parsed.files.find((f) => f.name === MANIFEST_NAME)!.data.toString("utf8"),
    ) as AssetManifest;
    manifest.version = version;
    const files: BundleFile[] = parsed.files.map((f) =>
      f.name === MANIFEST_NAME
        ? { name: MANIFEST_NAME, data: Buffer.from(JSON.stringify(manifest, null, 2), "utf8") }
        : f,
    );
    return this.publishFiles(files, `rollback:v${toVersion}`);
  }

  /** Auto-publish the placeholder bundle as v1 when nothing was ever published. */
  async ensurePlaceholderPublished(): Promise<PublishedEntry | null> {
    if (this.meta.history.length > 0) return null;
    const version = 1;
    const files: BundleFile[] = [
      {
        name: MANIFEST_NAME,
        data: Buffer.from(JSON.stringify(placeholderManifest(version), null, 2), "utf8"),
      },
      ...[...generatePlaceholderDataFiles().entries()].map(([name, data]) => ({ name, data })),
    ];
    return this.publishFiles(files, "placeholder");
  }
}

function readMeta(file: string): PublishedMeta {
  if (!existsSync(file)) return { current: 0, history: [] };
  try {
    const parsed = JSON.parse(readFileSync(file, "utf8")) as PublishedMeta;
    if (!Array.isArray(parsed.history)) return { ...EMPTY_META };
    return { current: parsed.current ?? 0, history: parsed.history };
  } catch {
    return { ...EMPTY_META };
  }
}

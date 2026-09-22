/**
 * Draft skin storage: converted RGB565 frames waiting to be published.
 * Drafts live under data/assets/draft/; publishing packs them (with
 * placeholder fallbacks for missing slots) into data/assets/bundle_v{N}.bin.
 */

import path from "node:path";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { readJson, writeJson } from "../store/jsonStore.js";
import type { ActionSlot } from "./placeholder.js";

export interface DraftAction {
  fps: number;
  w: number;
  h: number;
  frames: number;
  /** file name (inside draft dir) per frame, in order. */
  files: string[];
}

export interface DraftDeco {
  w: number;
  h: number;
  file: string;
}

export interface Draft {
  actions: Partial<Record<ActionSlot, DraftAction>>;
  map: { w: number; h: number; file: string } | null;
  decorations: DraftDeco[];
  updatedAt: string | null;
}

const EMPTY_DRAFT: Draft = { actions: {}, map: null, decorations: [], updatedAt: null };

export class DraftStore {
  private readonly dir: string;
  private draft: Draft;

  constructor(assetsDir: string) {
    this.dir = path.join(assetsDir, "draft");
    this.draft = readJson<Draft>(path.join(this.dir, "draft.json"), structuredClone(EMPTY_DRAFT));
  }

  get(): Draft {
    return this.draft;
  }

  private async writeFrame(name: string, data: Buffer): Promise<void> {
    await mkdir(this.dir, { recursive: true });
    await writeFile(path.join(this.dir, name), data);
  }

  async setAction(slot: ActionSlot, frames: Buffer[], fps: number, w: number, h: number): Promise<void> {
    const files: string[] = [];
    for (let i = 0; i < frames.length; i++) {
      const name = `${slot}_${i}.bin`;
      await this.writeFrame(name, frames[i]!);
      files.push(name);
    }
    this.draft.actions[slot] = { fps, w, h, frames: frames.length, files };
    await this.touch();
  }

  async setMap(frame: Buffer, w: number, h: number): Promise<void> {
    await this.writeFrame("map.bin", frame);
    this.draft.map = { w, h, file: "map.bin" };
    await this.touch();
  }

  async setDecorations(decos: { w: number; h: number; data: Buffer }[]): Promise<void> {
    const list: DraftDeco[] = [];
    for (let i = 0; i < decos.length; i++) {
      const name = `deco_${i}.bin`;
      await this.writeFrame(name, decos[i]!.data);
      list.push({ w: decos[i]!.w, h: decos[i]!.h, file: name });
    }
    this.draft.decorations = list;
    await this.touch();
  }

  async clearSlot(slot: ActionSlot): Promise<void> {
    const a = this.draft.actions[slot];
    if (a) {
      for (const f of a.files) await rm(path.join(this.dir, f), { force: true });
    }
    delete this.draft.actions[slot];
    await this.touch();
  }

  async clearAll(): Promise<void> {
    await rm(this.dir, { recursive: true, force: true });
    this.draft = structuredClone(EMPTY_DRAFT);
    await this.touch();
  }

  async readFrame(file: string): Promise<Buffer> {
    return readFile(path.join(this.dir, file));
  }

  private async touch(): Promise<void> {
    this.draft.updatedAt = new Date().toISOString();
    await mkdir(this.dir, { recursive: true });
    await writeJson(path.join(this.dir, "draft.json"), this.draft);
  }
}

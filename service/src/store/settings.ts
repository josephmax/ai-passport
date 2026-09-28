/**
 * Service settings + account keys, persisted under data/.
 *
 * P1 stores API keys in PLAINTEXT at data/keys.json (mode 0600). This is a
 * documented risk: anyone with file access to the host (or a data/ backup)
 * reads the keys. Encrypt at rest before exposing the service beyond a fully
 * trusted machine. The portal only ever echoes the last 4 characters.
 */

import path from "node:path";
import { readJson, writeJson, writeJsonSync } from "./jsonStore.js";
import { CITY_TABLE } from "../cityTable.js";

export type Provider = "local" | "claude" | "glm" | "deepseek" | "codex";

export interface ServiceSettings {
  primaryAccount: Provider;
  city: string | null;
  customLat: number | null;
  customLon: number | null;
  /** null = device renders the plain number, no percentage (spec §4.1). */
  weeklyTokenBudget: number | null;
  /** Name shown beside today's Token use on the badge; empty uses firmware placeholder. */
  badgeName: string;
  badgeRole: string;
  claude: {
    label: string;
    weeklyCapHours: number;
    rolling5hCapHours: number;
    projectsPath: string | null;
  };
  glm: { label: string; codingPlanUrl: string };
  deepseek: { label: string; balanceUrl: string };
}

export const DEFAULT_SETTINGS: ServiceSettings = {
  primaryAccount: "local",
  city: CITY_TABLE[1]!.id, // shanghai
  customLat: null,
  customLon: null,
  weeklyTokenBudget: null,
  badgeName: "",
  badgeRole: "",
  claude: {
    label: "Claude",
    // Defaults mirror spec §4.3 example values; adjust in data/config.json.
    weeklyCapHours: 140,
    rolling5hCapHours: 36,
    projectsPath: null,
  },
  glm: {
    label: "GLM",
    codingPlanUrl: "https://open.bigmodel.cn/api/paas/openapi/resource/coding-plan",
  },
  deepseek: {
    label: "DeepSeek",
    balanceUrl: "https://api.deepseek.com/user/balance",
  },
};

export interface ServiceKeys {
  glm: string | null;
  deepseek: string | null;
}

const DEFAULT_KEYS: ServiceKeys = { glm: null, deepseek: null };

export class SettingsStore {
  private readonly configFile: string;
  private readonly keysFile: string;
  private settings: ServiceSettings;
  private keys: ServiceKeys;

  constructor(dataDir: string) {
    this.configFile = path.join(dataDir, "config.json");
    this.keysFile = path.join(dataDir, "keys.json");
    this.settings = { ...DEFAULT_SETTINGS, ...readJson<ServiceSettings>(this.configFile, DEFAULT_SETTINGS) };
    // Older installs selected the Claude-only collector. Local Agents now
    // includes Claude alongside other detected CLI agents.
    let migrated = false;
    if (this.settings.primaryAccount === "claude") {
      this.settings.primaryAccount = "local";
      migrated = true;
    }
    if ((this.settings.primaryAccount as string) === "chatgpt") {
      this.settings.primaryAccount = "codex";
      migrated = true;
    }
    if (migrated) {
      // Persist the normalized id so the file stops carrying the legacy value.
      writeJsonSync(this.configFile, this.settings);
    }
    this.keys = readJson<ServiceKeys>(this.keysFile, DEFAULT_KEYS);
  }

  get(): ServiceSettings {
    return this.settings;
  }

  async update(patch: Partial<ServiceSettings>): Promise<ServiceSettings> {
    const next: ServiceSettings = {
      ...this.settings,
      ...patch,
      claude: { ...this.settings.claude, ...(patch.claude ?? {}) },
      glm: { ...this.settings.glm, ...(patch.glm ?? {}) },
      deepseek: { ...this.settings.deepseek, ...(patch.deepseek ?? {}) },
    };
    this.settings = next;
    await writeJson(this.configFile, next);
    return next;
  }

  getKeys(): ServiceKeys {
    return this.keys;
  }

  getKey(provider: "glm" | "deepseek"): string | null {
    return this.keys[provider];
  }

  async setKey(provider: "glm" | "deepseek", key: string | null): Promise<void> {
    this.keys = { ...this.keys, [provider]: key };
    await writeJson(this.keysFile, this.keys, 0o600);
  }
}

/** Mask a key for display: last 4 characters only. */
export function maskKey(key: string | null): string {
  if (!key) return "";
  const tail = key.length <= 4 ? key : key.slice(-4);
  return `•••• ${tail}`;
}

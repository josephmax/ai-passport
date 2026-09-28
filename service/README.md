<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# AI Passport Local Service

Usage source boundaries and the next collection architecture are documented in
[the usage collection plan](../docs/development/engineering/usage-collection.md)
and [ADR 0005](../docs/adr/0005-separate-usage-quota-and-billing.md).

Self-hosted backend for the AI Passport multi-pendant (see
[`docs/specs/2026-09-22-multi-pendant-app.md`](../docs/specs/2026-09-22-multi-pendant-app.md) and
[`docs/specs/2026-09-22-config-portal.md`](../docs/specs/2026-09-22-config-portal.md)):
collects Coding-account usage, aggregates the device usage snapshot, transcodes
pet skin PNGs to RGB565 asset bundles, forwards weather, and hosts the mobile
config portal. The device talks only to this service (ADR-0001); PNG→RGB565
transcoding happens server-side (ADR-0004).

- Runtime: Node.js ≥ 22, TypeScript (strict), single-process Fastify + sharp.
- Scope: P1 (ccusage local Agent reports, GLM/DeepSeek API collectors; subscription
  allowance APIs remain separate).

## Install

```bash
cd service
npm install            # add --registry=https://registry.npmmirror.com if the default registry is slow
cp .env.example .env   # then edit PORTAL_PASSWORD / SESSION_SECRET
npm run build          # tsc -> dist/
```

## Run

```bash
npm start              # node dist/index.js
npm run start:local    # build + detached local service; creates private .env only if absent
npm run dev            # tsx watch src/index.ts (auto-reload)
npm test               # node:test unit suite (pure logic only)
npm run smoke          # boots the server briefly and exercises pair/snapshot/bundle
npm run gen:placeholder # generate + inspect the default placeholder bundle v1
```

On first start, if no asset bundle has ever been published, the service
auto-publishes a code-generated **placeholder bundle v1** (running figure /
sword-fight / moon-sleep / trophy-victory actions, gradient map strip, bush
decoration, rain/snow sprites) so a fresh device can download v1 out of the box.

## Configuration

Environment (`.env`, see `.env.example`):

| Variable | Default | Meaning |
| --- | --- | --- |
| `PORT` | `3000` | HTTP port (device API + portal share it) |
| `HOST` | `0.0.0.0` | Bind address; must be reachable from the device and phone |
| `PORTAL_PASSWORD` | — | Config-portal login password (required; without it the portal is disabled, device API unaffected) |
| `SESSION_SECRET` | ephemeral | Cookie-signing secret (set it to keep logins across restarts) |
| `CLAUDE_CONFIG_DIR`, `CODEX_HOME`, etc. | Agent defaults | Optional ccusage data roots on the service host; see [supported sources](https://github.com/ccusage/ccusage#supported-sources) |
| `SNAPSHOT_REFRESH_MINUTES` | `10` | Background snapshot refresh interval |
| `DATA_DIR` | `service/data` | Runtime data directory |

Runtime settings live in `data/config.json` (created with defaults on first run):

```jsonc
{
  "primaryAccount": "local",         // local Agent token report first
  "city": "shanghai",                // built-in city table (33 Chinese cities)
  "customLat": null, "customLon": null, // overrides the city when both set
  "weeklyTokenBudget": null,         // null = device shows the plain number
  "glm":     { "label": "GLM",      "codingPlanUrl": "https://open.bigmodel.cn/api/paas/openapi/resource/coding-plan" },
  "deepseek":{ "label": "DeepSeek", "balanceUrl": "https://api.deepseek.com/user/balance" }
}
```

Most active settings can also be changed from the portal. Existing `claude`
settings in older config files are retained for compatibility but are no longer
used by the local Agent collector.

### API keys (P1 security note)

GLM/DeepSeek API keys entered in the portal are stored **in plaintext** at
`data/keys.json` (file mode 0600). This is an accepted P1 trade-off: the
service is meant to run on your own machine, LAN-only. Anyone with file access
to the host (or a copy of `data/`) can read the keys; the portal only ever
echoes the last 4 characters. Encrypt at rest before exposing the host beyond
fully trusted users.

## Device API

| Endpoint | Auth | Description |
| --- | --- | --- |
| `POST /api/pair` | pairing code | Body `{"code":"123456","deviceName":"..."}`. 6-digit one-shot code (10 min TTL, generated in the portal). Success: `200 {"token":"<32hex>"}`; wrong/expired/used code: `403`; missing `deviceName`: `400`. |
| `GET /api/snapshot` | `X-Device-Token` header | The usage snapshot (below). `401` on bad/revoked token. Updates the device's last-sync time. |
| `GET /assets/bundle_v{N}.bin` | none (LAN, version-gated) | Published APB1 asset bundle. `404` if that version was never published. |
| `GET /api/health` | none | Liveness probe. |

### Snapshot schema (spec §4.3)

```jsonc
{
  "schema": 1,
  "generatedAt": "2026-09-22T06:00:00+08:00",
  "accounts": [{
    "provider": "local",
    "label": "Local Agents",
    "quotas": {
      "weekly": null,
      "rolling5h": null,
      "weeklyTokens": { "used": 412000, "cap": null, "unit": "tokens", "resetAt": "...", "percent": null }
    }
  }],
  "dailyTokens": { "used": 32000, "coverage": "local-agent-logs" },
  "badgeName": "Example",
  "badgeRole": "Role",
  "weather": { "code": 61, "kind": "rain", "sunrise": "06:12", "sunset": "18:05", "city": "Shanghai" },
  "assetBundle": { "version": 3 }
}
```

Additive fields beyond the spec: `percent` (server-computed, one decimal, null
without a cap — devices only render), `kind` (normalized weather), and quota
`basis` / account `error` for degraded providers. Rules:

- Only **connected** accounts appear; the primary account is always `accounts[0]`.
- A quota a provider cannot measure is `null` — never fabricated:
  - **DeepSeek** remaining money is an additive account `balance` reading
    `{ remaining, currency, basis }`; its Token quota fields stay null.
  - **GLM** is experimental. Only explicit hours/requests and a recognized
    300/10080-minute window can map to quotas; ambiguous readings stay null.
  - **Codex quota** reads recent local `rate_limits` observations, maps windows
    by duration (not primary/secondary order), and expires samples after one
    hour or their reset. Unit is `%`; no vendor credential is read. This is
    the current local Codex profile, not ChatGPT web quota or a bill.
- A failing collector never breaks the snapshot: the account stays listed with
  `error` and null quotas.
- The local Agent collector invokes pinned `ccusage` in offline JSON mode and
  sums its daily rows from Monday 00:00 through today. It detects Claude Code,
  Codex, OpenCode, Gemini CLI and other supported local sources on the **service
  host**. No logs or API keys leave that host. Agent token counts do not measure
  official subscription allowance or usage percentage.
- With the local collector, `dailyTokens.used` is the observed local Agent
  total and `coverage` is `local-agent-logs`. Finance-only accounts do not
  erase that total, and provider counts are not added again. A failed local
  reading stays null.
  The badge fields come from the authenticated Badge page. The device retains
  the last snapshot when offline.

## Asset bundles (APB1)

Byte-exact container (little-endian throughout):

```
0x00  4  magic "APB1"
0x04  2  version (u16 LE)
0x06  2  file_count (u16 LE)
then file_count entries:
  u16 name_len (LE) + name_len bytes UTF-8 relative path
  u32 data_len (LE) + data_len bytes raw content
```

Must contain `manifest.json` plus frame files. Frame data = per-frame RGB565
pixels concatenated, each pixel one u16 **little-endian** (LVGL native), frame
size = w×h×2 bytes. Publish limit: 4 MB per bundle.

Slot rules enforced at upload: actions 64×64 (run/fight 4–8 frames, sleep 2–4,
victory ≤4), map 240×160, decorations 24×24 (≤8). fps 1–10. Rain/snow weather
sprites (16×16, 2 frames) are always the built-in generated set in P1.

## Config portal

`http://<lan-ip>:<PORT>/portal` — password login (session cookie, 8 h). Pages:

- **Badge**: edit the name and secondary role shown beside today's Token total on Home. The
  firmware font accepts only the characters in `main/fonts/badge_name_glyphs.txt`
  plus ASCII letters, digits, spaces, dots, underscores, and hyphens; unsupported
  names are rejected before sync. The role accepts up to 10 supported ASCII
  characters. An empty name shows the device placeholder.
- **Accounts**: GLM/DeepSeek key forms (echo last 4 only), local Agent collector
  status and last collection, primary-account selector (default local),
  Codex local quota observations.
- **Preferences**: city (built-in table + custom lat/lon), weekly token budget
  (null = plain number on the device).
- **Devices**: device list (name/token status/last sync), generate one-time
  pairing code, revoke tokens (effective immediately).
- **Assets**: upload PNGs per slot (multipart), server-side validation and
  RGB565 transcode, draft → publish (auto-incremented version) → history →
  rollback (old version republished under a NEW version number). Missing draft
  slots fall back to placeholder art at publish time.

Portal security scope (P1): LAN + password, no CSRF tokens (forms only),
no public exposure. Do not port-forward the service.

## Directory layout

```
service/
├── src/
│   ├── index.ts               entry: env, stores, placeholder publish, listen
│   ├── app.ts                 Fastify factory (cookie/formbody/multipart)
│   ├── deviceApi.ts           /api/pair, /api/snapshot, /assets/*
│   ├── snapshot.ts            §4.3 snapshot builder (pure)
│   ├── snapshotService.ts     cache + scheduled refresh
│   ├── weather.ts             open-meteo + 30-min cache (pure mapper)
│   ├── wmo.ts                 WMO normalization (firmware mirror)
│   ├── xpTable.ts             XP segmentation mirror (checks: 3145 total)
│   ├── cityTable.ts           33-city table + location resolution
│   ├── collectors/            localAgents.ts (ccusage),
│   │                          glm.ts / deepseek.ts (mappers + clients), registry.ts
│   ├── assets/                bundleFormat.ts, rgb565.ts, pipeline.ts (sharp),
│   │                          placeholder.ts, draftStore.ts, publisher.ts
│   ├── store/                 settings.ts (config + keys), devices.ts, jsonStore.ts
│   ├── portal/                routes.ts (pages + forms), session.ts, html.ts
│   └── util/time.ts           local-offset ISO, ISO week window
├── test/                      node:test suites (pure logic + fs tmpdirs)
├── tools/                     gen-placeholder-bundle.ts, smoke.sh
├── data/                      runtime (gitignored): config.json, keys.json,
│                              devices.json, assets/ (bundles + draft)
└── dist/                      build output (gitignored)
```

## Testing

`npm test` runs the pure-logic suites: WMO normalization against the full
0–99 range (mirrors `main/app/app_weather.c`), XP segmentation table (3145
tomatoes to level 100), pairing-code lifecycle, APB1 pack/parse incl. the 4 MB
cap, RGB565 endianness, local Agent report parsing,
GLM/DeepSeek response mappers, snapshot field/order/percent rules, city table,
publisher draft→publish→rollback lifecycle, and the sharp PNG pipeline.
`npm run smoke` additionally boots the real server end-to-end.

## Not implemented in P1

- NFC tap-to-join/open onboarding and the cohesive two-stage phone experience;
  the current device SoftAP form is entered manually from Settings. See the
  [onboarding design and handoff](../docs/specs/2026-09-24-badge-onboarding.md).
- Claude subscription quota polling, ChatGPT web quota, and actual API billing
  are not implemented. Codex local quota observations are available.
- Portal asset animation preview and snapshot preview rendering (P2).
- Weather sprite uploads (built-in set only), per-device forced bundle re-push,
  multi-device grouping (P3+), encrypted key storage, HTTPS (device uses plain
  HTTP on the LAN in P1; add a reverse proxy if you need TLS).


## Local acceptance handoff (2026-09-26)

The service build, 77 unit tests, and the real-log smoke gate passed: login,
manual refresh, single-use pairing, authenticated snapshot, unauthorized rejection,
asset download, and the 4096-byte device snapshot limit. The Accounts page shows
observed today/week Tokens and any fresh Codex quota. A running local instance was
also checked against a separately invoked ccusage daily report. Actual GLM and
DeepSeek account queries remain unverified without their keys; no account-wide
or invoiced-cost completeness is claimed.

Open `http://localhost:3000/portal/accounts`. The detached process uses ignored
`service/.env` and `service/data/`; read `PORTAL_PASSWORD` in `.env` for login.
`npm run start:local` reuses a reachable instance; it does not restart it after
source edits. For a restart, identify the process listening on the configured
port, stop that service process, then run the command again. Sleep/reboot stops
availability; this helper does not install a system autostart job. A LAN device
uses the computer's LAN address, not localhost, and must pair through the portal.

Voice input research is in the [feasibility report](../docs/specs/2026-09-26-voice-input-feasibility.md).

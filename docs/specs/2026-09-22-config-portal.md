**English** · [简体中文](2026-09-22-config-portal.zh_CN.md)

# Config Portal Design Specification

- Date: 2026-09-22
- Status: partially implemented; the current design and remaining work are tracked in [badge onboarding design](./2026-09-24-badge-onboarding.md)
- Upstream: [Multi-pendant app spec](./2026-09-22-multi-pendant-app.md) · terminology in [`docs/CONTEXT.md`](../CONTEXT.md)
- Related ADRs: [0001 local service](../adr/0001-usage-proxy-holds-credentials.md) · [0004 server-side transcoding](../adr/0004-asset-pipeline-server-side-transcode.md)

## 1. Three configuration surfaces (responsibility split)

| Surface | Host | Manages |
| --- | --- | --- |
| SoftAP provisioning page | **device** (captive-portal mini page) | the three onboarding essentials: Wi-Fi credentials, service address, one-time pairing code |
| Device settings page | device (three-button UI) | brightness, volume, screen-off, rest window, sync now, pairing QR |
| Config portal | **local service** (web, phone browser) | account connections, primary account, city, token budget, the full asset chain, devices and tokens |

Principle: provider credentials and portal settings live on the service. The
device persists Wi-Fi/service address/token, local settings, focus/XP records,
and the asset bundle; usage and badge settings arrive through snapshots.

## 2. Original target information architecture

This section records the original target, not the current navigation. The implemented portal has **Badge, Accounts, Preferences, Devices, and Assets** tabs; Overview and its device-accurate snapshot preview are not implemented. See [the updated design and implementation matrix](./2026-09-24-badge-onboarding.md).

### 2.1 Overview
- Device card × N: online status, last sync, battery, firmware version, current bundle version
- Snapshot preview: renders the current snapshot as the device shows it (identical view)

### 2.2 Accounts
- Four provider cards:
  - **Claude**: collector run status, last collection time, log entry (collector credentials stay in collector config)
  - **GLM / DeepSeek**: API key form (currently stored as plaintext in a local file with mode 0600; echo shows last 4 chars only)
  - **ChatGPT**: P2 placeholder noting the missing official API
- Per-card live quota preview (weekly/5h/tokens; missing dimensions hidden)
- Footer: **primary account** radio (decides the three cards on the device dashboard main view)

### 2.3 Assets (P1 = upload + validation + static thumbnails; animation preview in P2)
- **Actions**: four slots — run / fight / sleep / victory. Each: upload PNG
  frame sequences (server transcodes to RGB565), set frame rate (1–10 fps),
  static thumbnail grid
- **Map**: upload the strip PNG (transcoded to 240×160 RGB565), static
  end-to-end seam preview
- **Decorations**: element library (upload 24×24 elements), per-element
  enable switch, on-screen count 1–2
- **Bundle versioning**: draft set → "publish" mints a new version → devices
  download on next sync; version history list + one-click rollback (rollback
  = republish the old version)

### 2.4 Preferences
- City (search-select; drives weather and sunrise/sunset)
- Weekly token budget (null = device shows the raw value without a percentage)

### 2.5 Devices
- Device list: name, token status, last sync
- **Generate one-time pairing code** (6 digits, valid 10 minutes, single use)
- Token revocation (a revoked device fails its next sync and returns to the
  unpaired state)
- Per-device "current bundle version" and force re-push

## 3. Pairing and provisioning flow (new device, out of the box)

1. In the current firmware, select Wi-Fi in device Settings to enter SoftAP mode. The original automatic-first-boot behavior remains unimplemented. The device shows the AP name (`Passport-XXXX`) on screen.
2. Phone joins that AP → open the device-hosted provisioning page. Automatic captive-portal opening depends on the phone and remains unverified.
3. The form has four fields: Wi-Fi SSID / password / service address +
   one-time pairing code (generated in the portal's devices tab)
4. Device saves → joins Wi-Fi → `POST /api/pair {code, deviceName}` → the
   service validates the code → issues a **device token**
5. Pairing done; the device starts hourly sync and its portal device list records the last request.

Pairing codes are one-time and short-lived; tokens can be revoked and
re-issued (provisioning runs again).

## 4. API sketch

| Endpoint | Direction | Notes |
| --- | --- | --- |
| `POST /api/pair` | device→service | exchange pairing code for device token |
| `GET /api/snapshot` | device→service | header `X-Device-Token`; snapshot JSON (main spec §4.3), includes `assetBundle.version` |
| `GET /assets/bundle_v{N}.bin` | device→service | asset bundle (manifest + frame data packed in order), streamed in chunks into LittleFS |
| `/portal/...` | browser→service | authenticated HTML pages and form posts for badge, accounts, preferences, assets, and devices |

Config portal access control: password session; deploy on a trusted LAN. Asset
uploads are multipart, with server-side size/frame validation and transcoding.

## 5. Change propagation latency (final: hourly + manual)

| Change | When the device sees it |
| --- | --- |
| Primary account / city / token budget | next snapshot (≤1 h), or device "sync now" |
| Account add/remove | same (affects snapshot contents) |
| Published new bundle version | next sync notices `assetBundle.version` change → download → atomic switch |
| Device-local settings (brightness etc.) | immediate (does not transit the service) |

## 6. Security checklist

- Provider credentials and the portal password stay on the service host; the device stores its own token together with Wi-Fi credentials and the service URL. API keys are currently plaintext in the service data directory, protected only by host file permissions.
- Revocation takes effect immediately (next sync rejected)
- Pairing codes: one-time, 10-minute expiry
- Deploy the portal on a trusted LAN only. The service currently binds to `0.0.0.0` by default; network exposure must be controlled by the host and must not be treated as an automatic LAN-only guarantee.
- Bundle validation server-side: dimensions/frame counts/package size cap
  (protects the 5 MB LittleFS)

## 7. Phasing

- **P1**: accounts tab (Claude/GLM/DeepSeek) + primary account, preferences,
  devices (pairing codes/revocation), badge, asset upload + validation + static
  preview + publish/rollback, SoftAP provisioning page, `/api/pair`
- **P2**: animation preview (play at frame rate, scrolling map with
  decoration overlay), ChatGPT collector card, snapshot preview rendering
- **P3+**: multi-device groups, remote access, asset sharing

**English** · [简体中文](2026-09-22-multi-pendant-app.zh_CN.md)

# Multi-Pendant Application Development Specification

- Date: 2026-09-22
- Status: existing application partially implemented; BLE firmware/macOS helper implemented; device acceptance pending
- 2026-09-24: §3 (pages and navigation) and §7.1 (settings interaction model) are superseded by the [Home Panel Redesign specification](./2026-09-24-home-panel-redesign.md); the remaining sections stay in force, and the new specification wins on conflict
- 2026-10-14: the [BLE companion MVP](2026-10-14-ble-companion-mvp.md) replaces device networking, phone onboarding, hourly HTTP delivery, and the local-ASR proposal. This revision updates the target; firmware now uses BLE; hardware acceptance remains pending. Reversal records: [ADR 0006](../adr/0006-ble-companion-without-device-networking.md), [ADR 0007](../adr/0007-microphone-bridge-reuses-desktop-dictation.md).
- Terminology: see [`docs/CONTEXT.md`](../CONTEXT.md); key decisions in [`docs/adr/`](../adr/)
- Target: FoloToy AI Passport (ESP32-C3, 8 MB Flash, no PSRAM, 240×320 ST7789P3, UP/DOWN/OK three buttons, ES8311 audio, no vibration motor)

## 1. Overview

One firmware with a single Home and second-level usage/settings views, a pet-skinned focus timer with XP/weather, and an explicit microphone mode. A local service retains collection, configuration, asset transcoding, and weather. A nearby computer companion exchanges snapshots and microphone audio with the device over BLE. The pendant does not join a network; the computer keeps its own internet connection. The microphone becomes a desktop audio input, with recognition/cleanup/insertion handled by the user's existing Typeless application.

Product north star: **"a living pendant"** — the pet is the personified
shell, data is its mood source, the Pomodoro is its daily life.

## 2. System architecture

### 2.1 BLE architecture (implemented 2026-10-15; hardware acceptance pending)

```text
Host browser -> local service (collectors, snapshot, settings, PNG/APB1)
                     | authenticated loopback access
                     v
                Mac companion <--- BLE ADPCM/status --- pendant
                     |         --- BLE snapshot/time -> pendant
                     v
             virtual microphone -> Typeless -> active text field
                                      | cloud recognition/cleanup
```

Provider credentials remain on the host. The device persists computer binding, local settings, focus/XP, last snapshot, and current assets; no Wi-Fi credentials or service address are required by the new flow. BLE pairing and stream details, migration preservation, and M0–M3 gates are authoritative in the [BLE companion specification](2026-10-14-ble-companion-mvp.md). BLE asset transfer is later work; current assets remain usable.

### 2.2 Original network architecture (superseded; retained for history)

The diagram and network credentials below record the original design. Its HTTPS label was aspirational; the superseded firmware used plain LAN HTTP. Do not implement this onboarding for the next MVP.

```
┌─ Phone browser ── Config Portal (Web, hosted by local service)
│                    │ account link / PNG asset upload / city / primary account
│                    ▼
│              ┌─ Local service ─────────────────┐
│              │ Collector plugins × N (OSS)     │
│              │  ├ Claude: read CC local logs    │
│              │  ├ GLM/DeepSeek: balance APIs    │
│              │  └ ChatGPT: research later (P2)  │
│              │ Snapshot agg / PNG→RGB565        │
│              │ Weather relay (open-meteo)       │
│              └──────────┬──────────────────────┘
│                         │ HTTPS · device token · hourly
│                         ▼
└─ AI Passport device firmware (this repo, feature/* branch)
   Shell navigation / Dashboard / Pet+Pomodoro / Settings
   NVS(state) + LittleFS(assets) + light-sleep timing + deep-sleep idle
```

The device holds zero vendor credentials and stores only: Wi-Fi credentials,
service address, device token (all writable/resettable via the SoftAP
provisioning page).

## 3. Pages and navigation

Three top pages cycle with UP/DOWN. **Key map (final)**:

| Context | UP/DOWN short | OK short | OK long |
| --- | --- | --- | --- |
| Dashboard main | switch top page | drill into selection | none |
| Dashboard drill-down | switch among the three details | — | back to main |
| Pet page | switch top page | start/stack a focus block | cancel all blocks |
| Settings page | switch top page | enter/adjust option | back/exit option |

After screen-off wake, return to the page shown before screen-off. On
victory, regardless of the current page, jump automatically to the pet page,
play the victory animation + sound, and stay there.

## 4. Dashboard

### 4.1 Main view

Four compact summary cards, with the first three fed by the **primary account**
(selectable in the config portal, default Local Agents):

1. **Weekly quota**: large percentage + used/cap small text + reset countdown (days)
2. **5-hour quota**: percentage + remaining time in the current window
3. **Weekly tokens**: percentage (denominator = the weekly token budget set
   in the portal; if unset, show the raw value)
4. **Agents**: number of local Agent sources with a separate log reading

The selected card is highlighted; OK drills in. Top status bar: sync time
(`x min ago`), computer/BLE connection state, battery. Collector freshness and connection state are separate.

### 4.2 Drill-down pages

UP/DOWN switches among the four cards at the top level. The first three details
show the primary account's numbers plus a per-account row list for the same
dimension. The Agents detail shows one local source at a time: daily/weekly
Tokens and its 5-hour/weekly quota, with UP/DOWN switching sources. OK
long-press returns to the cards.

### 4.3 Usage snapshot (service → device)

```jsonc
{
  "schema": 1,
  "generatedAt": "2026-09-22T06:00:00+08:00",
  "servedAt": "2026-09-22T06:00:02+08:00",
  "dailyTokens": { "used": 412000, "coverage": "local-agent-logs" },
  "badgeName": "Joseph", "badgeRole": "Developer",
  "agents": [
    { "agent": "codex", "dailyTokens": 400000, "weeklyTokens": 900000,
      "rolling5h": null, "weekly": { "used": 3, "cap": 100, "unit": "%", "resetAt": "..." } },
    { "agent": "pi", "dailyTokens": 12000, "weeklyTokens": 21000,
      "rolling5h": null, "weekly": null }
  ],
  "accounts": [{
    "provider": "claude",            // claude|glm|deepseek|chatgpt
    "label": "work",
    "quotas": {
      "weekly":      { "used": 62,   "cap": 140, "unit": "h", "resetAt": "..." },
      "rolling5h":   { "used": 18,   "cap": 36,  "unit": "h", "resetAt": "..." },
      "weeklyTokens":{ "used": 412000, "cap": null }   // null cap = no budget, show raw value
    }
  }],
  "weather": {
    "code": 61,                       // WMO code, normalized to clear/overcast/rain/snow
    "sunrise": "06:12", "sunset": "18:05",
    "city": "Shanghai"
  },
  "assetBundle": { "version": 3 }     // later asset capability; MVP keeps installed assets
}
```

Device rules: the snapshot lands in NVS (one JSON string); badge name and role
also have their own NVS key, rewritten only when changed. Agent detail shows
per-source daily/weekly observed Tokens and independently sampled 5-hour/weekly
quota; unavailable quotas are null. While offline, show the last known data
tagged "offline x h". Percentages are computed server-side; the device renders
the readings.

## 5. Pet

### 5.1 Actions and rendering

| Action | Trigger | Frame spec |
| --- | --- | --- |
| Run | default state | 64×64, 4–8 frame loop |
| Fight | while a focus block runs | 64×64, 4–8 frame loop |
| Sleep | inside the rest window, no block running | 64×64, 2–4 frame loop |
| Victory | all blocks finished | 64×64, one-shot 1–2 s |

Map strip: 240 wide × 160 high, scrolls sideways at constant speed, seamless
end-to-end loop; the character stays fixed at x ≈ 1/3 of the screen, grounded.
Time-of-day look uses **runtime tinting** (day = original colors / dusk =
warm / night = darkened blue, via LUT or LVGL styles); weather uses small
overlay sprites (rain streaks / snowflakes, one set of 2–3 animated frames
each; sun sparkle for clear; none for overcast) — **do not pre-bake 12 map
variants**.

### 5.2 Weather matrix (4 weather × 3 times of day)

Time of day derives from that day's sunrise/sunset in the snapshot:
day = sunrise+30 min ~ sunset−60 min; dusk = sunset−60 min ~ sunset+45 min;
everything else is night (margins at both ends to avoid flicker, with a
10-minute hysteresis). Weather from WMO code: 0–2 clear; 3/45/48 overcast;
51–67/80–82 rain; 71–77/85–86 snow. Offline keeps the last known values.

### 5.3 Head-up display and bottom info bar

- Top: `Lv.12` badge + a thin in-level progress bar (80 px wide)
- Bottom info bar (three items): `today 3 · week 18 · next level 7` (units:
  focus blocks)

### 5.4 Rest

Default sleep 23:00–08:00 next day (running animation stops, sleep action
shown, map stops scrolling). Start/end adjustable in the device settings page
(±30-minute steps). Starting a Pomodoro inside the window is still allowed
(the pet pulls an all-nighter fighting); victory volume follows the setting.

## 6. Focus timing and XP

### 6.1 State machine

```
IDLE ──OK short──▶ RUNNING(units n=1)
RUNNING ──OK short, n<5──▶ RUNNING(n+1)      // each press +25 min
RUNNING ──OK short, n=5──▶ short beep, no change
RUNNING ──OK long──▶ IDLE(cancelled, no XP)
RUNNING ──expiry──▶ VICTORY(animation+sound, grant XP n) ──▶ IDLE
```

- Screen-off/light sleep allowed while timing (ADR-0003): state (absolute end
  timestamp, unit count, granted-XP count) written to NVS on every change;
  after power loss, recompute from absolute time
- Each 25-minute unit boundary silently grants +1 XP; victory plays only
  when everything finishes
- On victory, jump to the pet page if elsewhere

### 6.2 XP and levels (banded fixed costs, unit = focus blocks)

| Current level L | To L+1 costs (blocks) | Pace reference (8 blocks/day) |
| --- | --- | --- |
| 1–9 | 5 | 1.6 levels/day |
| 10–29 | 15 | ~1 level per 2 days |
| 30–59 | 25 | ~1 level per 3 days |
| 60–98 | 50 | ~1 level per 6–7 days |
| 99 | 100 (endgame) | ~12 days |

Level 100 = 3145 cumulative blocks ≈ 1308 focused hours. Level = reverse
lookup in the banded table, pure integer math; Today/Week counts reset at
local midnight and Monday midnight respectively; XP and levels never reset.
All persisted in NVS.

## 7. Settings page (device side)

| Item | Form |
| --- | --- |
| Brightness | 5-step cycle (LEDC PWM) |
| Connect computer | enter time-limited authenticated BLE binding; show connection/retry status |
| Screen off | 15 s / 30 s / 1 min / 2 min |
| Rest time | start/end each adjustable ±30 min |
| Forget computer | confirm removal of the computer association, preserving local data |
| Volume | 0–5 steps (victory sound / beeps) |
| Microphone | enter explicit stream mode; OK start/stop, long OK stops and returns |
| Sync now | request latest host snapshot; report disconnected, queued, success/failure and data age |

This is the next target, not the current Wi-Fi/phone list. The [BLE specification](2026-10-14-ble-companion-mvp.md) replaces those entries and requires a layout/font check for the expanded list.


### 7.1 Settings interaction model (clarified)

The settings page uses a strict level model so level-one key behavior
stays unambiguous:

- **Top level**: one large "Settings" button only. UP/DOWN switches top
  pages; OK enters the list. Long-press: nothing.
- **List level**: UP/DOWN moves the cursor; OK selects the highlighted
  item; long-press returns to the top level.
- **Adjust state** (value items: brightness, volume, screen-off):
  entered by selecting the item. UP/DOWN changes the value (applied
  live); OK confirms and returns to the list; long-press cancels and
  reverts to the value at entry.
- **Flow screens** (binding, microphone, and multi-field rest window): selecting opens the next screen with
  its own local keys (shown in the bottom hint bar); long-press walks
  back one level.
- "Sync now" is an immediate action: OK runs it and stays in the list.

## 8. Device ↔ service protocol

- Next transport: one authenticated BLE GATT connection, with versioned control, snapshot, and audio messages; no device HTTP endpoint requests or IP configuration.
- Sync: push after binding/reconnect, successful host refresh, or configuration changes; manual "sync now" asks the companion for delivery. Host collection remains independently scheduled, rather than waiting for an hourly device pull.
- Payload: preserve the §4.3 snapshot and 4096-byte cap; complete, validate, and acknowledge before applying. Preserve last valid data on partial delivery and distinguish stale data from a disconnected link.
- Time: generate fresh `servedAt` on host delivery and preserve `generatedAt` as reading time. Keep existing older-snapshot parsing compatibility.
- Audio: device-to-host PCM notifications in explicit microphone mode; control/stop first, audio next, coalesce snapshots and defer their persistent application until capture stops.
- Assets: keep installed/built-in assets in the MVP. Later APB1 transfer uses BLE chunks and verified atomic LittleFS installation, not the old HTTP download. An unsupported bundle update must not block snapshots.

The original `POST /api/pair`, `GET /api/snapshot`, and `/assets/*` device requests remain in the current legacy implementation. They are historical transport contracts, not the next target. Full message semantics and acceptance are in the [BLE companion specification](2026-10-14-ble-companion-mvp.md).

## 9. Local service components

| Component | Responsibility | P1 approach |
| --- | --- | --- |
| Claude collector | weekly quota / 5h window / weekly tokens | reuse OSS (ccusage-style reader over Claude Code local usage logs), resident |
| GLM/DeepSeek collectors | balance/usage | official API + key (stored server-side) |
| ChatGPT collector | no official subscription API | **P2, under research** |
| Snapshot aggregation | normalize into the §4.3 schema | scheduled refresh + on demand |
| Config portal web | account link / primary / city / token budget / asset upload | host loopback + existing password session; see the [config portal spec](./2026-09-22-config-portal.md) |
| Mac companion | BLE binding/snapshot exchange and PCM-to-virtual-input bridge | native helper, CoreBluetooth + Core Audio; no ASR model |
| Asset pipeline | PNG→RGB565 frame packs + manifest, size validation | server-side sharp / custom transcode |
| Weather | open-meteo (no key, includes sunrise/sunset) | 30-minute cache |

## 10. Technology choices

| Area | Choice | Rationale | Rejected |
| --- | --- | --- | --- |
| Firmware framework | ESP-IDF 5.5.3 + repo BSP | established | Arduino compat layer |
| UI | LVGL (BSP-integrated) | established; build/destroy screens on switch to bound memory | — |
| Device transport | NimBLE authenticated GATT | office-network independence; one data/audio connection | mandatory Wi-Fi, SoftAP, NAT traversal |
| Desktop audio | native Mac helper + independently installed BlackHole 2ch | reuse a system microphone interface | custom driver/ASR/input method in MVP |
| JSON | cJSON | IDF-bundled, streamable, bounded memory | jsmn (too low-level) |
| Asset storage | LittleFS (`esp_littlefs`) | power-loss safe, file semantics | FATFS (wear, size) |
| State persistence | NVS | established | — |
| Timing power | light sleep + timer; deep sleep idle | ADR-0003 | deep sleep while timing |
| Clock | calibrate against the local service | tolerate offline drift | external RTC chip (absent) |
| Alerts | ES8311 speaker | **no vibration motor/buzzer on board**, the only option | vibration |
| Server | Node.js + TypeScript (single process: Fastify + sharp + collector supervision) | best fit for web/transcode/JSON; portal and pipeline in one repo | Go (fine, revisit if the team prefers) |
| Claude collection | OSS ccusage-style | user requested "find an OSS approach" | custom parser |
| Weather | open-meteo | keyless, has sunrise/sunset | providers needing keys |
| Tests | host-side unity-style (repo pattern) for pure logic | repo convention | — |

## 11. Memory and power budget (red lines)

- The next target has no device Wi-Fi/TLS stack. Current firmware's BLE-disabled setting must change; measure the resulting heap, DMA, largest block, and stacks rather than adding historical Wi-Fi/BLE estimates.
- The LVGL pool is 48 KiB after the 2026-10-16 BLE startup repair; Home stays resident. The native render stress run peaked at 36.5 KiB. This is not an ESP32 heap/stack measurement; see the [startup repair checkpoint](2026-10-14-ble-companion-mvp.md#10-startup-failure-and-repair-2026-10-16). Do not import the older voice report's 24 KiB estimate as a current fact.
- Audio uses bounded streaming queues, initially targeting at most 16 KiB device PCM storage; no full utterance or recording file. Freeze animation and defer snapshot persistence/assets during capture.
- Give capture exclusive codec ownership; stop/acknowledge effects before capture, and inhibit automatic sleep while streaming. Focus/XP continue; victory audio/navigation wait until capture stops.
- Later assets are bounded writes, never a whole bundle in RAM. Keep existing power-state invariants outside active microphone mode. Board sleep/streaming currents and endurance require measurement; chip-level µA figures are not product promises.

## 12. Testing and acceptance

Host unit tests (pure logic, no hardware): the focus state machine
(stacking/cancel/power-loss recompute/cross-unit XP grants), the banded XP
table and today/week resets, weather normalization and time-of-day
hysteresis, snapshot parsing and offline tagging.

Device acceptance (before P1 ships, item by item): three-page navigation and
the full key map; drill-down and back; 25/50/125-minute blocks surviving
screen-off, on-time page jump + sound; rest-window sleep/wake behavior; the
12 weather×time combinations eyeballed; authenticated BLE binding/reconnect without provisioning; a
24-hour offline snapshot tag; asset bundle v1→v2 hot update; overnight deep
sleep drain; spot checks of the level curve (forge XP in NVS to verify
display). Asset transfer acceptance applies to the later BLE asset increment, not the first MVP. Add all M0–M3 gates in the [BLE specification](2026-10-14-ble-companion-mvp.md), including virtual microphone, no-network sync, overflow/stop/reconnect, and measured audio quality/memory/power. Historical firmware acceptance is not evidence for this new transport.

## 13. Milestones

Current next delivery: **M0 desktop virtual-microphone compatibility → M1 BLE data/binding → M2 BLE microphone → M3 combined acceptance**, defined in the [BLE MVP](2026-10-14-ble-companion-mvp.md). Large-asset transfer, phone/NFC onboarding, multi-device support, remote shortcuts, and local recognition are deferred.

### Original P1–P3 roadmap (superseded where it requires networking)

- **P1 (wearable daily)**: shell+navigation, dashboard (local service +
  Claude/GLM/DeepSeek collectors), pet with three actions + placeholder AI
  assets + focus timing + XP/levels + light-sleep continuation, the 12
  weather combinations (sunrise/sunset), victory sound, all device settings,
  SoftAP provisioning, hourly sync, config portal minimal (account link +
  primary account + city)
- **P2**: full custom-asset chain (upload/transcode/hot update/decoration
  configuration), ChatGPT collector (feasibility permitting), 7-day mini bar
  chart on drill-down
- **P3 (optional)**: multiple devices, decoration count/density config, more
  actions

## 14. Risks and open assumptions

1. **ChatGPT subscription quota has no official API** (P2; may downgrade to
   an API-usage basis or be cut)
2. **GLM Coding Plan quota semantics** need server-side research (official or
   derivable basis)
3. **UI language defaults to Chinese** — needs an embedded font subset
   (~100–200 KB Flash, within the 3 MB firmware budget); switching to English
   later is free. **Pending final confirmation**
4. ccusage-style OSS tooling can break as Claude Code's local format
   evolves; collector plugins isolate the blast radius
5. RTC drift during deep sleep: corrected on every sync; long offline periods
   may shift the rest window (acceptable)

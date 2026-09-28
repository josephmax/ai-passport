**English** · [简体中文](2026-09-24-badge-onboarding.zh_CN.md)

# Badge onboarding and config portal: design and handoff

- Date: 2026-09-24
- Status: design accepted; badge and existing portal implemented, NFC onboarding pending
- Scope: this `feature/multi-pendant-p1` application, not the upstream hardware-test baseline
- Related: [Home design](2026-09-24-home-panel-redesign.md), [config portal target](2026-09-22-config-portal.md), [service manual](../../service/README.md)

## Accepted product decision

The NFC tap should lead to **one recognizable entry** with two stages: first connect and pair the device, then manage its badge, accounts, assets, preferences, and devices. The provisioning form is hosted by the device's SoftAP; the daily settings portal is hosted by the local service. They cannot be one always-available HTTP page because the phone changes networks during onboarding. Keep their language and visual hierarchy consistent, and hand off from the provisioning result to the service portal when reachable. A manual path through device Settings and the phone browser must always work.

The Home card combines the owner's name, optional role, and today's Token total; OK still opens usage details. The walking pet is left of the map's three completed-tomato readouts (today, lifetime total, remaining to next level). The five dots below the map describe the **current timer**, not lifetime totals. The authoritative current Home layout and render are in the [Home design](2026-09-24-home-panel-redesign.md) and `main/ui/ui_pet.c`.

### Wireframe: intent, not a screenshot of shipped UI

```text
Device, 240 × 320                   Phone, one entry / two phases
┌────────────────────────┐          ┌──────────────────────────────┐
│ sync age       Wi-Fi  % │          │ PASSPORT · connect / manage  │
│ Joseph     TODAY TOKEN  │          │ 1 Connect  →  2 Manage       │
│ Developer       200M    │          │                              │
│ Lv.6 ━━━        10:04   │          │ Connect: join device hotspot │
│                        │          │   Wi-Fi / service URL / code │
│   walking pet   Today 3 │          │   connect, pair, show result │
│                 Total 36 │          │                              │
│                 Need 4  │          │ Manage: Badge | Accounts    │
│                        │          │   Preferences | Devices     │
│ focus dots / timer  ⚙  │          │   Assets                     │
└────────────────────────┘          └──────────────────────────────┘
```

The mobile design uses a dark header, light content surface, mint accent, short progress steps, a visible device/network state, and a preview of the badge. The connection stage should explain when to join or leave the device hotspot; the management stage should show the real service state. Demo values in a mockup must never be interpreted as actual device status, credentials, or usage.

The wireframe uses **Joseph / Developer** and **200M today's Tokens** as approved display examples. The 200M figure is illustrative, not measured usage. The earlier firmware preview used that value as a fallback; the current source removes it and shows an unavailable value until a valid reading arrives.

The [interactive visual concept](badge-setup-concept.html) is kept in the repository for review. It predates the final Home tomato layout and contains illustrative states; this document and the current firmware define implementation status.

## What exists in this checkout

| Area | Current implementation | Source and verification |
| --- | --- | --- |
| Home badge and tomato progress | Name/role + daily Token card, map readouts, focus/settings navigation; local fallback identity before sync | `main/ui/ui_pet.c`, `main/app/app_xp.c`, [Home design](2026-09-24-home-panel-redesign.md); host LVGL render passed |
| Badge configuration | Authenticated `/portal/badge` saves validated name/role; snapshot carries `badgeName`/`badgeRole` | `service/src/portal/routes.ts`, `service/src/portal/badgeName.ts`, `service/src/snapshot.ts`; service tests passed |
| Daily portal | Accounts, Preferences, Devices, Assets, and Badge tabs; pair code, pairing API, snapshot, asset publishing | `service/src/portal/routes.ts`, `service/src/deviceApi.ts`, [service manual](../../service/README.md) |
| Manual onboarding | Device Settings starts SoftAP; device page collects Wi-Fi, service URL, and one-time code; worker joins Wi-Fi and pairs | `main/ui/ui_settings.c`, `main/app/app_prov.c`, `main/app/app_sync.c`; the full phone/device path is not yet accepted on hardware |
| Manual sync feedback | Settings shows unpaired/disconnected, queued/in progress, and snapshot success/failure | `main/ui/ui_settings.c`, `main/app/app_sync.c`; built and host checked, not yet accepted on hardware |

The actual portal is functional but visually simpler than the proposed staged mobile concept. It does not yet provide a live device preview. The current SoftAP form is also a minimal separate page; there is no shared mobile shell or automatic handoff to the service portal.

## Remaining work, in implementation order

1. **On-device acceptance of the new Home and sync feedback.** The exact image below was written to the application partition and its device hash verified on 2026-09-24. Inspect badge, all three tomato values, focus control, settings feedback, wake and navigation. Then pair to a reachable local service and verify that a saved portal badge appears after manual sync. The device display and complete sync path still await user confirmation.
2. **Make onboarding a cohesive flow.** Add clear connect/pairing/error/retry states to the device-hosted page and a transition to the service portal when the phone can reach it. Keep the current manual Settings path. Test wrong Wi-Fi, unreachable service, expired code, reconnection, and no captive popup.
3. **Investigate NFC behavior on target phones.** The board has a passive NTAG213 with ordinary NDEF read/write and no MCU-facing API. No NFC payload programming, tap handler, or tested phone flow exists here. Choose a stable entry URL or records only after testing real phones and the network transition. Do not promise that a single tap will both join Wi-Fi and open the portal across phones. Provide explicit manual instructions when either OS action is not available.
4. **Polish the daily portal.** Bring the existing Badge/Accounts/Preferences/Devices/Assets pages toward the shared design, add truthful connection status and preview, and preserve the existing auth and form behavior. An Overview tab and animation preview remain separate future work.

Acceptance for NFC onboarding: from an unpaired device, a tap or the documented fallback reaches connection instructions; the phone can join the SoftAP, submit four fields, see the pairing result, return to the LAN, and reach the authenticated service portal. Repeat on each supported phone/OS. No NFC record may contain Wi-Fi passwords, API keys, portal passwords, pairing codes, or device tokens.

## Reproducing and handing over safely

The verified firmware bundle was built locally at `build/firmware/6af4b711769e056a1d859a1a3507e5dd2a1296b8bb814815a853387a2ebed799/` (full SHA-256 `6af4b711769e056a1d859a1a3507e5dd2a1296b8bb814815a853387a2ebed799`, application SHA-256 `d0f7043879b452987c2c9ea3e7a04d5f5eb14b0ef419d16135726461cd98a53c`, matching ELF SHA-256 `ef086796f1ec05edcefa25995dd72f19c5d55285cb8397069e257ee6e73f2401`). The complete `./tools/validate.sh` gate and host LVGL render passed. On 2026-09-24 the application was flashed at `0x10000–0x20651f` after explicit consent; the device partition-table hash matched the archive, and esptool verified the written data. NVS settings/focus records, PHY, and assets were not written. The user had reported a generic name and no Token figure before this latest flash; post-flash visual and service-sync results are pending. This bundle is ignored by Git and may not exist in another checkout; rebuild and verify there. Any later image needs its own flash consent.

The subsequent 200M preview image passed the complete gate and archive verification at `build/firmware/711a05ec10003ca61341a0d76e7c6ff3948f07c64c7fc1782304621eeda26cd2/`. Its full SHA-256 is `711a05ec10003ca61341a0d76e7c6ff3948f07c64c7fc1782304621eeda26cd2`, application SHA-256 is `3d5548554bb7342f04a1df420de78e390aa12e97e0f74a8c1c005ab289ab22d8`, and matching ELF SHA-256 is `b5a8dc7a9d3dc5b519ea9bc324161390e7d7a62d68f7be061050213b67e6bc24`. On 2026-09-24, after consent for this exact image, the existing partition table was read and matched SHA-256 `daabdd6c37c2dea6e5b31413fbc3f1201ed114cfd87ac72433ef649ddfac267f`; only the application at `0x10000–0x20651f` was flashed, and esptool verified its written hash. NVS, PHY, and assets were preserved. On-screen 200M confirmation is pending user observation.

On 2026-09-28, the source removed the temporary 200M fallback, distinguished Codex from ChatGPT, hid previous-day local usage at midnight, and separated `servedAt` clock calibration from cached `generatedAt`. The complete gate and service smoke passed. The new verified local archive is `build/firmware/f162443e000ac845ae15490144b43ddaf264371953e25c7bfc9b8c42a68e68ee/` (full SHA-256 `f162443e000ac845ae15490144b43ddaf264371953e25c7bfc9b8c42a68e68ee`, application SHA-256 `d49f7d426e3ac5c5976833c9b8e335d595941e7e5c1018f9c8f169a72dcb6587`, ELF SHA-256 `ea20a896df8bf1001864318d34c217a2548ce6bd677cecac1736adc526cd465e`). The application occupies `0x10000–0x20675f`; this image has **not** been flashed. The local service is running on its configured port with observed daily/weekly Agent readings, but a real device sync still needs acceptance.

Owner-specific identity defaults may be supplied through Git-ignored `main/app/app_identity_local.h`; runtime portal settings are in ignored `service/data/`. Neither is transported with Git. A fresh checkout uses the generic name placeholder until configured and synced. The local service had a badge configured but no device record when checked; pairing with that service remains unverified. User-approved display examples in this design are not live measurements. Do not put credentials, codes, tokens, device identifiers, or raw logs in tracked docs or fixtures. Inspect `git status --short --branch` before editing, preserve unrelated work, and follow `AGENTS.md` for validation and fresh flash consent.

On 2026-09-28, the badge gained a separate device NVS key, rewritten only when the name or role changes. The usage snapshot and device detail page now retain per-agent daily/weekly Tokens and independent 5-hour/weekly quota slots. A live service packet contained Pi, ZCode, Codex, and Claude Code and measured 1,390 bytes, within the 4 KB device limit. Only Codex weekly quota had a valid observation in that sample; missing quotas remain null. The complete gate passed and the verified bundle is `build/firmware/179cea8ac0d0e6ec068b4b54f8ec7754ff874a2e192339554c3648225ab4f472/` (full SHA-256 `179cea8ac0d0e6ec068b4b54f8ec7754ff874a2e192339554c3648225ab4f472`, app SHA-256 `03d0df024d5a052fa736f6cdbe312f757c3ff18edc8f5822f28fa02116064b6c`, ELF SHA-256 `bc79c1f8e369568f1886e5948ed30dddcfa89d6597a84d397dad4052c2086096`). This image has not been flashed or device-tested.

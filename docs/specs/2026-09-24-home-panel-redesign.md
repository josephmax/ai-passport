**English** · [简体中文](2026-09-24-home-panel-redesign.zh_CN.md)

# Home Panel Redesign Specification

- Date: 2026-09-24
- Status: visual design accepted and implemented; device acceptance pending
- Audience: UI designer (function points and interaction constraints) / firmware engineering (implementation basis)
- Glossary: see [`docs/CONTEXT.md`](../CONTEXT.md)
- Supersedes: [Multi-Pendant specification](./2026-09-22-multi-pendant-app.md) §3 (pages and navigation) and §7.1 (settings interaction model); the remaining sections of that specification stay in force, and this document wins on conflict

## 1. Background and goal

Cycling through three top pages proved disorienting in real use: the Dashboard and Settings top pages carry no core information themselves — they are merely drill-down launchers and page-switch corridors. This redesign removes that launcher layer and collapses to a **single Home + second-level lists** stack:

- one Home screen with the pet scene always resident, shown directly at boot;
- data and settings are reached through cursor targets on Home into their second-level lists;
- every second/third-level view returns level by level with a long OK press, ultimately to Home;
- waking from screen-off always lands on Home.

The north star is unchanged: **a living pendant** — the pet is the personified shell, data is its mood source, focus timing is its daily routine. The resting Home shows "pet + today's token spend", delivering the two highest-frequency readings with zero key presses.

## 2. Information architecture

```
Home (the only top level; boot target; wake target)
 ├─ Headline: today's token spend (large number)
 ├─ Cursor targets (UP/DOWN, wraps around):
 │    ① headline      ② focus indicator (5 dots)      ③ settings icon
 │    ① OK ─▶ Usage page (second level)
 │    ② OK ─▶ focus-count adjust (embedded Home state)
 │    ③ OK ─▶ Settings list (second level)
 └─ Pet scene resident (actions/map/weather/time-of-day per prior spec §5)

Usage page (second) ──OK──▶ drill-down detail (third) ── long press returns level by level
Settings list (second) ──OK──▶ adjust state / flow page (third) ── long press returns level by level
```

## 3. Home function points

| # | Element | Status | Notes |
| --- | --- | --- | --- |
| 3.1 | Top status bar | kept | Sync-age text + Wi-Fi icon + battery percentage (battery hides on read failure). Kept on Home and on every second-level list. |
| 3.2 | Today token spend headline | new | The informational anchor of Home, rendered as a large number; scope and formatting in §8. Cursor target; OK opens the Usage page. |
| 3.3 | Level badge + in-level progress bar | kept | Scenery only; not cursor-selectable, no interaction. Current bar width is 80px, adjustable by design. |
| 3.4 | Clock | kept | Scenery, no interaction. |
| 3.5 | Pet scene | kept | Actions (run/fight/sleep/victory), 240×160 map strip, weather overlays, time-of-day tinting all follow prior spec §5, unchanged here. |
| 3.6 | Focus indicator (5 dots) | kept+extended | Dual role: run-state display and cursor target. Visual treatment of granted/active/unused dots is a design decision. OK enters the focus-count adjust flow (§4). |
| 3.7 | Settings icon | new | Cursor target; OK opens the Settings list. Position and form by design. |
| 3.8 | Cursor | new | UP/DOWN move among the three targets, wrapping; default position = focus indicator (OQ1). Visual form by design. |

Removed elements: the bottom "today/week/to-next-level" text strip and any trace of the three-page cycle. Completed tomato counts now live inside the map, leaving the bottom action area clear.

## 4. Focus-count adjust flow (state machine embedded in Home)

Principle: the indicator holds a count setting `n ∈ 0..5`; confirming with OK means "read n → restart a whole countdown of n×25 minutes". **While adjusting, any running countdown is not disturbed.**

```
IDLE ──OK (indicator)──▶ ADJUST (initial n = last setting, else 1)
ADJUST: UP/DOWN ─ n±1, clamped to 0..5; pressing up at 5 = brief beep, no change
         at n=0 the surface reads as "cancel"
ADJUST ──OK──▶ n≥1: RUNNING (countdown n×25min from this moment, restarted)
                n=0: IDLE (cancel all timing, no XP granted)
ADJUST ──long OK──▶ IDLE (discard the change; a running countdown is unaffected)
RUNNING ──OK (indicator)──▶ ADJUST (initial n = this run's unit count)
RUNNING ──unit boundary (every 25min)──▶ silently +1 XP (granted count persisted)
RUNNING ──all finished──▶ VICTORY (animation+sound, auto-jump to Home) ──▶ IDLE
```

- Every confirmation is a **full re-read**: changing to n=4 mid-run and confirming makes the total 4×25min counted from the confirmation moment (progress restarts). This semantics is an explicit product choice; a mis-confirmation discards elapsed progress, so the ADJUST surface must carry an explicit "confirming restarts the countdown" slot (OQ5).
- Granted XP is never revoked; cancellation grants none.
- Power-loss persistence follows prior spec §6.1: absolute end timestamp, n, granted count in NVS; recomputed from absolute time after reboot.
- Light sleep while timing / deep sleep when idle (ADR-0003) unchanged.
- The only entry point for cancelling all timing = reducing to 0 in ADJUST and confirming (OQ2 lists the alternative).

## 5. Usage page (second level, reuses the current dashboard implementation)

- Entry: OK on the Home headline target; long OK returns to Home.
- Content: the current three cards (weekly quota / rolling 5-hour quota / weekly tokens, primary-account scope) are reused as-is; card selection, OK drill-down, and the drill-down page (third level) are preserved — only the long-press return target changes from "dashboard main view" to "Usage page → (long press again) Home".
- UP/DOWN change from "switch top page" to "move the cursor among the three cards".
- The snapshot schema gains a today aggregate field (§8); it is P1-required — it feeds the headline and is independent of any future dashboard-page extension.
- Known scope difference (OQ6, not reconciled for now): the headline is "today, sum of all accounts" while the cards are "weekly, primary account". Extending the dashboard page (adding a today row, etc.) is a separate requirement.

## 6. Settings list (second level, reuses the current implementation)

- Entry: OK on the Home settings icon; long OK returns to Home.
- Rows unchanged: brightness / Wi-Fi (provisioning) / screen timeout / rest window / connect phone / volume / sync now.
- Interaction follows prior spec §7.1 layering with one change — every "long press returns to the top page" becomes "long press returns to Home":
  - List: UP/DOWN move the cursor; OK selects; long press returns to Home.
  - Adjust state (brightness / volume / screen timeout): UP/DOWN adjust live; OK confirms and returns to the list; long press cancels and restores the entry value.
  - Flow pages (provisioning / rest window / connect phone): keys per the in-page bottom hint bar; long press returns level by level.
  - "Sync now" is an immediate action: OK triggers it and stays in the list.
- When a focus victory auto-jumps to Home while the user is in an adjust state: jump after **restoring the entry value** with the "long-press cancel" semantics (OQ3).

## 7. Key map (finalized)

| State | UP/DOWN short | OK short | OK long |
| --- | --- | --- | --- |
| Home | move cursor among the three targets (wrapping) | headline→Usage page; indicator→enter adjust; settings icon→Settings list | no action (reserved) |
| Focus adjust (ADJUST) | n±1 (0..5, beep at the cap) | confirm: n≥1 restarts countdown; n=0 cancels all | discard change; running countdown unaffected |
| Usage page | move cursor among three cards | drill into selected card | return to Home |
| Drill-down detail | switch among the three details | — | return to Usage page |
| Settings list | move cursor among rows | enter / adjust / sync now | return to Home |
| Adjust state | adjust value live | confirm, return to list | cancel and restore entry value |
| Flow page | defined per page | defined per page | return to Settings list |
| Victory animation | ignored | ignored | ignored |

## 8. Headline data scope and formatting

- Scope: Token usage observed in supported Agent logs on the **service host** since local midnight (not an all-account bill or the primary-account scope). The service delivers it with the snapshot; the device only renders.
- Schema: the snapshot has top-level `dailyTokens`, e.g. `"dailyTokens": { "used": 832000, "coverage": "local-agent-logs" }`. The device may retain an offline snapshot, but hides its today-only value after local midnight. The service collects at midnight and every 10 minutes by default; an awake device pulls hourly or on manual sync.
- Formatting rules (pure-logic function, covered by host tests):
  - value < 1000: plain integer, no unit (`832`);
  - otherwise 4 significant digits, **truncated toward zero (no rounding)**; units K/M/B/T step at 1e3/1e6/1e9/1e12, using the largest unit with value ≥ 1;
  - examples: 832,000→`832.0K`; 1,234,567→`1.234M`; 1,999,999→`1.999M`; 12,345,678→`12.34M`; 123,456,789→`123.4M`; 1,234,567,890→`1.234B`; 1.5e13→`15.00T`;
  - cap: values ≥ 1e16 display `9999T`.

## 9. Designer constraints and mandatory elements

- Canvas 240×320 (RGB565); input is three keys only (UP/DOWN/OK, OK supports long press); no touch.
- Chinese UI: new strings enter the font-subset budget (every glyph costs Flash); keep copy terse.
- Mandatory Home elements (none may be missing): status bar, today-spend headline, Level badge + progress bar, clock, pet scene (240×160 map strip), focus indicator (5 dots), settings icon, cursor visual.
- Widget-count red line: no PSRAM; Home keeps a fixed, small widget count; the build-and-destroy page memory model is unchanged. Every decorative widget must pass a memory accounting review.
- Forms the design must produce: cursor selected-state, layout of the three targets and the settings icon form, the ADJUST surface (including the "confirming restarts the countdown" text slot), second-level list visuals (row height/font size/selected state), drill-down page visuals.

## 10. Open questions (defaults chosen, reviewable)

| # | Question | Current default |
| --- | --- | --- |
| OQ1 | Default cursor position on Home | focus indicator (fewest presses for the frequent action) |
| OQ2 | Entry point for cancelling a running timer | reduce to 0 in ADJUST and confirm; alternative: long press on the indicator outside adjust |
| OQ3 | Semantics when a victory jump interrupts an adjust state | restore the entry value |
| OQ4 | Whether the cursor wraps around | yes |
| OQ5 | Whether ADJUST explicitly says "confirming restarts the countdown" | yes (text slot left to design) |
| OQ6 | Scope gap between headline (today/all accounts) and cards (week/primary account) | not reconciled in-page for now; dashboard extension is a separate requirement |
| OQ7 | Shape of the `dailyTokens` field | top-level aggregate in the snapshot |

## 11. Engineering impact and acceptance additions

- Refactor scope: `main/ui/ui_shell.c` (page cycle removed), `main/ui/ui_dash.c` (demoted to second level), `main/ui/ui_settings.c` (re-parented), `main/ui/ui_pet.c` (absorbs headline/settings icon/cursor).
- New host tests: headline formatting function (boundaries and truncation), the new focus state machine (adjust / full re-read restart / reduce-to-zero cancel / power-loss recompute), the today aggregation scope.
- Service side: snapshot aggregation gains `dailyTokens`.
- Acceptance additions (merge into prior spec §12): all Home target paths; full ADJUST state machine including mid-run value change; reduce-to-zero cancel; victory jumping out of a settings adjust state with the interrupted item restored to its entry value; wake always lands on Home; headline offline fallback + sync-age label; formatting boundaries (1e3/1e6/1e9/1e12/1e16, truncation not rounding).

## 12. Accepted visual implementation (2026-09-24)

The accepted Night Voyage design uses the existing navy/mint palette and Glitch
Rabbit bundle. The 240×320 home places status at y=4, the daily headline at y=23,
the unchanged 240×160 map at y=86, a non-interactive level/clock HUD over the map,
focus and settings targets at y=252, and key hints at y=303. The adjustment draft
replaces the headline, with an explicit restart warning; the map remains visible.
Selection uses an inset mint outline without shifting content. Usage has three
72px cards. Settings shows seven 32px rows, ordered brightness, Wi-Fi, screen-off,
rest, phone connection, volume, sync. Rest editing steps by 30 minutes; OK advances
to the end field then saves, while long OK discards the draft.

`dailyTokens.used` is the service host's observed local Agent Token usage since
local midnight, with coverage `local-agent-logs`; it is not an all-account bill.
If that collector cannot supply a valid reading, the aggregate is `null` and the
device shows an unavailable value. The earlier `200M` firmware preview fallback
has been removed; it remains only in the design mockup. The service refreshes at
local midnight and masks the previous day's cached count while the new reading
is pending. Actual zero remains zero, and an offline device retains its last
received snapshot until the next successful sync. The unit rule
uses K/M/B/T, so 1,234,567,890 renders as `1.234B`.

Focus drafts are volatile. The last confirmed count uses a separate NVS key;
existing settings/focus/XP blob layouts and the partition table are retained.
Confirmed replacement settles elapsed unit rewards under the same mutex as
periodic polling, then restarts from the confirmation time. Storage runs on the
input worker, outside the LVGL lock. A wake gesture returns home without activating
a target. Forced exits restore unconfirmed settings before the victory screen.

Validation covers home navigation/drafts, truncation boundaries, snapshot fallback,
restart/cancel/reboot accounting, and service aggregation. Device acceptance still
requires Chinese glyph/rendering checks, page transitions under memory pressure,
physical keys, audio, wake-to-home, and victory interruption of an adjustment.

A native LVGL smoke run also renders Home, focus adjustment/cancellation, usage
and all settings flows using the actual generated fonts and bundled sprites.
The exercised labels have no missing glyphs or clipping; save/rollback assertions
and 30 repeated page cycles pass. Peak LVGL pool use is about 31 KiB on the host,
which is not a measurement of ESP32 heap or stack use. The configured pool uses
the current `LV_MEM_SIZE` option and retains the previously effective 64 KiB.

Delivery checkpoint: the complete repository gate passes under ESP-IDF 5.5.3;
the local service passes 62 tests and TypeScript compilation. This is the normal
25-minute build, without the fast-focus or boot-time clock injection switches.
The verified local archive is
`build/firmware/e56cb5aa187bada34d6a6310dd2b96fb598f042e9ad4aa798e2ed735749cc439/`.
Full-image SHA-256: `e56cb5aa187bada34d6a6310dd2b96fb598f042e9ad4aa798e2ed735749cc439`;
matching ELF SHA-256: `6a6414dd1d976406a9114acaa0492a086f6b9b0f8bbf58cd390e662b6bd1e096`.
Build: PASS. Host tests: PASS. Device tests: PASS for segmented flashing,
verified image hashes, startup, and user-observed Home display/basic key
navigation, focus start/discard/cancel, settings brightness rollback, and
screen-off/wake-to-Home without activating a target. The device partition table
exactly matched the archive; bootloader,
partition table, and app were flashed at `0x0`, `0x8000`, and `0x10000` without
touching NVS, PHY, or assets. A competing background serial reader initially
interrupted application transfer; after stopping it, the application write and
verification completed. The ESP32-C3 booted the new app with LVGL and assets
initialized, and no error or reboot loop appeared in a 15-second log window.
The 25-minute completion/victory audio, live all-account daily data, and
extended stability remain unverified until their physical checks finish.

## 13. Badge and pet position update (2026-09-24)

The selectable Home headline now shows the **name and today's Token total** in
one card. UP/DOWN still selects it and OK still opens the existing usage details.
Focus adjustment and victory temporarily replace its content. The name is edited
at `/portal/badge` in the local service, stored in service settings, and delivered
as `badgeName` and `badgeRole` in the snapshot. The optional role sits below the
name. Older snapshots and an empty name display the device's default identity;
without local configuration, that is the default name placeholder. A Git-ignored
`main/app/app_identity_local.h` can provide the name and role before the first
network sync. A snapshot with a valid service name overrides both defaults.
The field accepts only CJK glyphs included in the 24 px
firmware font and supported ASCII, up to eight display cells (CJK counts as two).
Wide Latin names switch to the 12 px font on the device. The glyph inventory is
`main/fonts/badge_name_glyphs.txt`; a different unsupported CJK name requires
expanding that inventory and rebuilding firmware. This is currently a
service-level setting, so devices sharing one service show the same name.

The pet now sits at x=84, making room for three compact readouts embedded at
the right of the scrolling map: completed tomatoes today, total completed
tomatoes, and tomatoes remaining to the next level. They read the existing
persisted XP state; the bottom five-dot focus control still describes only the
current timer. The map still scrolls behind the pet, while the labels stay fixed.
The manual-sync row now reports pairing/network prerequisites, request progress,
and the result of an attempted snapshot refresh. The new firmware rendering and
sync feedback still need on-device acceptance.

## 14. Current implementation checkpoint

Section 12 records acceptance of the earlier firmware. Section 13 is a later
change and has **not** inherited that device-test result. The latest source
renders a single selectable name/role + daily Token card, with `PET_X=84` and
three fixed map readouts sourced from persisted `today_count`, `total_xp`, and
`app_xp_to_next(total_xp)`. The status row for manual sync now distinguishes
unpaired, disconnected, requested/in progress, success, and failure. A host LVGL
render with the real generated fonts found no missing glyphs or clipped labels;
the complete `./tools/validate.sh` gate passed with ESP-IDF 5.5.3.

The verified local archive and hashes are recorded in the [onboarding handoff](2026-09-24-badge-onboarding.md).
Its app image was flashed on 2026-09-24 with device hash verification; visual
acceptance still includes the actual badge/tomato layout, focus and wake
controls, sync feedback, and a paired service round trip. The NFC entry and two-stage mobile flow remain
design-only; their scope and acceptance are in that handoff. Owner-specific
local defaults are Git-ignored.

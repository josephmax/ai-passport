**English** · [简体中文](2026-09-26-voice-input-feasibility.zh_CN.md)

# Voice input feasibility: BLE microphone and existing desktop dictation

- Updated: 2026-10-15
- Status: BLE capture and macOS bridge implemented 2026-10-15; Typeless/device acceptance pending
- Authority: [BLE companion MVP](2026-10-14-ble-companion-mvp.md), [ADR 0006](../adr/0006-ble-companion-without-device-networking.md), [ADR 0007](../adr/0007-microphone-bridge-reuses-desktop-dictation.md)

## Current deliverable and boundary

Deliver a microphone, not a new input method. The pendant streams audio over the same BLE connection used for data sync, with no Wi-Fi provisioning. On macOS a native helper receives independently framed ADPCM and decodes PCM, keeps bounded buffers, handles clock drift, and routes audio through a virtual input. Typeless consumes that system microphone and owns dictation, semantic cleanup, and text insertion. No whisper.cpp/FunASR inference, model download, clipboard manipulation, or synthetic desktop navigation is required in this MVP.

```text
ES8311/I2S -> bounded PCM queue -> BLE -> Mac bridge
  -> BlackHole 2ch -> Typeless client <-> cloud AI -> active text field
```

The existing BSP provides `bsp_audio_read()`, 16 kHz/16-bit mono configuration, I2S DMA, and a fixed 30 dB input gain. The three-second capture/playback demo remains in the repository but is excluded from the current app build. Firmware now captures continuous audio through the shared audio worker; the native desktop bridge receives ADPCM and routes decoded PCM. AGC/noise suppression/VAD remain absent. Gain, clipping, wearing distance, and noise must be measured before adding DSP; driver availability is not audio-quality acceptance.

The ESP32-C3 USB controller is fixed-function Serial/JTAG, so software alone cannot enumerate this connector as USB Audio/HID. BLE custom audio data also does not automatically create a standard Bluetooth headset microphone. A desktop virtual-audio bridge is required for the accepted existing-hardware path. USB serial remains a debug alternative, not first-use onboarding. [USB hardware limitation](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-guides/usb-serial-jtag-console.html).

## macOS integration and user interaction

Use an independently installed [BlackHole 2ch](https://github.com/ExistentialAudio/BlackHole#route-audio-between-applications) for the prototype: the helper outputs pendant audio to its channels, and Typeless selects the corresponding microphone. This is application-specific routing; do not route all system sounds into the microphone. Resample and map mono channels to the device's actual negotiated format. A custom branded driver and bundling/distribution are later work; resolve BlackHole licensing before embedding or redistributing it.

[Typeless supports explicit microphone selection](https://www.typeless.com/help/quickstart/settings), but that does not establish compatibility with this complete bridge. Test the input meter, selected channels/rate, idle silence, permissions, first/last syllable, and the effect of “Mute when dictating” on the bridge. If that setting mutes the routed stream, disable it for this prototype and record the result.

The device has a separate microphone screen with OK start/stop and long OK stop/return. The audio stream and a Typeless Dictation Session are separate: use Typeless's existing desktop shortcut after the stream is ready. Do not silently start listening on reconnect. Device-triggered shortcuts/hold-to-talk are deferred; the current button API lacks a release event, and the shared ADC ladder does not support assumed key chords. Typeless owns insertion permissions and behavior; the bridge does not need Accessibility permission merely to route audio.

## Running cost, cloud dependency, and AI cleanup

Official sources checked **2026-10-14**:

| Component | Work performed | Cost/boundary |
| --- | --- | --- |
| Firmware and Mac bridge | BLE, buffering, format conversion, audio routing | CPU/RAM/radio power only; no ASR/LLM models or inference API key; actual load unmeasured |
| Prototype virtual device | BlackHole loopback | Independently installed open-source driver; distribution is a separate licensing decision |
| Typeless Free | Dictation and writing features | Limited weekly words, standard accuracy; current canonical pricing page does not specify the count |
| Typeless Pro yearly | Unlimited words, enhanced accuracy, priority access | USD 144/year, equivalent to USD 12/month billed yearly |
| Typeless Pro monthly | Same Pro service | USD 30/month |

Prices and limits may change; the user's app/checkout is authoritative for purchase. [Pricing](https://www.typeless.com/pricing).

Typeless performs cloud transcription and processes audio with limited app/text context. Its stated zero-retention policy and local history storage are not offline inference. The computer needs internet access to Typeless; local BLE/microphone operation does not. Work-network admission for the pendant is solved, but desktop cloud-service reachability and acceptable handling of spoken/contextual work information remain separate deployment constraints. [Data controls](https://www.typeless.com/data-controls).

Typeless advertises removing filler/repeated words, keeping the final intent after a spoken self-correction, formatting lists, and adapting style/context. These are its writing features, not capabilities we implement or guarantee; test names, numbers, mixed-language technical terms, and deliberate corrections. Its exact internal models/pipeline are not specified by this design. [Dictation capabilities](https://www.typeless.com/).

Offline recognition is a future independent adapter: it can consume this microphone or received PCM, but would add model/inference resource costs and its own text-insertion/cleanup responsibilities. It is not a hidden dependency of the BLE microphone MVP.

## Resource and acceptance contract

Raw PCM is 32,000 bytes/s; the implemented independent ADPCM transport is 172 bytes per 20 ms (8,600 bytes/s). Capture uses one PCM/encoded frame on a fixed task stack rather than retaining a one-minute 1.92 MB utterance. Neither firmware nor bridge writes audio files by default. M0 virtual-input compatibility, M1 BLE data/binding, M2 microphone, and M3 combined gates, priorities, failure behavior, clock handling, migration, and RAM/power measurements are defined once in the [BLE MVP](2026-10-14-ble-companion-mvp.md).

Current code serializes capture and effects on one audio owner; `app_audio_fx_stop()` is checked by playback; the configured LVGL pool is 64 KiB, not 24 KiB. No current measured BLE/audio heap headroom exists. Previous provisional heap budgets, inference model footprints, and old firmware acceptance cannot prove this increment fits or works.

## Superseded proposal retained for history (2026-09-26)

The original report below proposed Wi-Fi WebSocket, local whisper.cpp/FunASR, and direct text/key insertion. **ADR 0006/0007 replace that scope and transport.** Its model comparisons, button mappings, cloud exclusion, resource estimates, and “current code” statements are historical research, not instructions for the next MVP. Retained to explain what was overturned; current requirements and validation are above.

### Scope and conclusion

Requested scope: cross-platform desktop dictation, macOS first. The pendant captures microphone audio and sends three-button actions; the computer receives audio, runs recognition, and inserts text or mapped navigation/confirmation keys into the focused application. This is feasible as an optional exclusive mode. This document is research and an implementation proposal; no voice firmware, desktop receiver, microphone accuracy, or runtime memory headroom has been validated yet.

The existing ES8311/I2S capture path supports 16 kHz, 16-bit mono PCM through `bsp_audio_read()`. The ESP32-C3 USB peripheral is fixed-function Serial/JTAG, not programmable USB Audio or HID. Consequently the existing USB connector cannot become a standard USB microphone/keyboard by adding TinyUSB. Wi-Fi with a desktop companion is the first transport; a framed USB serial transport is a possible later alternative and must separate binary audio from console logs. Bluetooth LE is not Bluetooth Classic headset audio; adding BLE HID while retaining Wi-Fi adds another radio/lifecycle integration and does not solve microphone transport.

### Proposed first increment

```text
Pendant: mic + button events
  -> authenticated LAN WebSocket, binary PCM + bounded control messages
  -> desktop receiver: session validation, audio buffering, recognition adapter
  -> whisper.cpp inference process (or a FunASR adapter)
  -> desktop input bridge: text insertion and mapped keys
```

Keep inference in a separate process so model loading and CPU work cannot block the Token service. The receiver must run on the computer being controlled, or forward to an input bridge on that computer; a NAS alone cannot insert text into a Mac. The first UI uses click-to-start/click-to-stop recording because the current BSP exposes press, click, double-click, and long-press, but no release event. Hold-to-talk requires a tested release event and suppression of the trailing click. Buttons share one ADC ladder, so do not depend on chords.

Provisional configurable mappings for voice mode only:

| Button event | Default action | Configurable alternatives |
| --- | --- | --- |
| UP click | Left arrow | Up, Home, previous item |
| DOWN click | Right arrow | Down, End, next item |
| OK click while idle | Enter | Tab, confirm preview |
| OK long press | Start/stop recording | Hold-to-talk after release-event support |
| UP double-click | Backspace | Escape, cancel preview |
| DOWN long press | Exit voice mode | Reserved local action |

Configuration should choose from named actions, not arbitrary shell commands. Transcribed text must not imply Enter; Enter remains an explicit button action. Capture the target application at recording start and require unchanged focus before insertion, otherwise keep the result for confirmation. macOS needs Accessibility permission for insertion/key events; Windows and Linux need platform adapters, with elevated Windows applications and Wayland requiring separate acceptance. Initially use text insertion/paste and discrete keys. A full OS input-method editor with composition and candidate windows is a larger independent feature.

### Open-source reuse

| Project | Useful capability | Integration work still required |
| --- | --- | --- |
| [whisper.cpp](https://github.com/ggml-org/whisper.cpp) | Local multilingual recognition, Apple acceleration, HTTP server | Receive ESP PCM, form the inference request, manage model lifetime, input bridge |
| [Handy](https://github.com/cjpais/Handy) | Cross-platform offline dictation, desktop paste and shortcut handling | Documented CLI toggles local recording; it is not a documented ESP PCM ingest API. Extend its audio source or reuse its input approach; verify the chosen release |
| [FunASR](https://github.com/modelscope/FunASR) | Chinese streaming recognition, punctuation, WebSocket protocol | Select a streaming model/runtime; heavier deployment; model weights have separate licenses |

Recommendation: first use multilingual Whisper small/base on the computer and final text after stopping. Avoid `.en` models for Chinese. Use a FunASR streaming adapter later if measured Chinese accuracy or interactive latency warrants it. Do not claim any of these projects is a ready-made network microphone plus three-button input device.

### Cost and resource budget

The following transport numbers are calculations, not measurements. At 16,000 samples/s × 2 bytes, PCM is **32,000 bytes/s** (256 kbit/s before overhead), **1.92 MB/minute**. A 20 ms frame is 640 bytes; a 16 KiB queue covers approximately 512 ms. A 60-second full recording is 1.92 MB and cannot be retained in this board's RAM; buffer the utterance on the computer. On overflow or disconnect, abort the utterance with a visible error instead of silently dropping speech or growing the buffer. Start with a configurable 60-second utterance limit on the host.

| Board resource | Proposed budget/boundary |
| --- | --- |
| PCM queue | Fixed 16 KiB |
| Capture/network task stacks | Initial 10–12 KiB total; verify high-water marks |
| Framing, control queue, socket/codec allocations | Budget remaining capacity; total added active heap target **40–64 KiB**, provisional |
| I2S DMA | Existing BSP creates six 240-frame descriptors per channel; measure actual allocations, not just PCM payload |
| Display | Keep the existing 9.6 KB partial DMA buffer and 24 KB LVGL pool; a small status UI, no full-screen buffer |
| TLS | Existing product spec budgets a 40–50 KB handshake peak; measure before selecting WSS. Do not overlap handshakes with an active stream |
| Flash | No ASR weights on ESP; firmware delta must be measured after implementation |

The application has no PSRAM. No current build map or trustworthy live minimum-free-heap/largest-block measurements were available during this review, so **40–64 KiB is a design target, not proof that it fits**. Entry into voice mode must stop and acknowledge audio playback, prevent the audio worker from suspending the codec during capture, defer asset downloads and snapshot networking, hold the pet animation on a static frame, and disable automatic sleep until capture exits. Keep focus time/XP state running without its network/audio side effects. The existing `app_audio_fx_stop()` only sets a flag and its playback loop does not currently check that flag; add a bounded cancellation/ownership handshake before integrating capture.

Local recognition has no per-minute API fee. The upstream whisper.cpp unquantized memory table lists roughly 388 MB for base, 852 MB for small, 2.1 GB for medium, and 3.9 GB for large; these are reference model footprints, not total desktop process memory or latency promises. Quantization can reduce footprint. Model-specific CPU/GPU load, Chinese quality, and end-of-speech delay must be benchmarked on the target Mac. Cloud ASR is optional and excluded from the first increment. Wi-Fi streaming and an active microphone increase power draw and inhibit sleep; battery life requires measured streaming current and battery capacity, not an invented estimate. No board replacement is proposed for the bounded microphone mode; all-day always-on ASR, on-board large-model recognition, and simultaneous asset sync are outside the first increment.

### Acceptance before enabling the feature

1. Record baseline free internal heap, minimum free heap, largest internal block, DMA heap, and stacks in the existing firmware. Repeat while connecting, capturing, stopping, and returning Home. Measure firmware Flash size.
2. Verify a fixed buffer never grows with utterance length; handle slow receiver, queue overflow, Wi-Fi disconnect, receiver exit, malformed frames, cancellation, and duplicate button messages without unintended keys.
3. Run at least 30 minutes and 100 start/stop cycles with the existing pet/focus/settings state preserved. Check codec ownership, no reboot/watchdog, no task/buffer leak, and complete stream teardown before sleep.
4. Measure audio clipping/noise and transcription error on Chinese, English, mixed technical terms, normal distance, and background noise; compare models on the same recordings. Measure end-to-text latency and active current.
5. Test Mac text editor, browser input, terminal, focus change, clipboard restoration, and permission denial. Then test Windows and Linux separately. Verify arrows/Enter/Backspace and configuration changes; Enter must happen only on its mapped event.

### Primary references

- [Board audio and memory constraints](../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md)
- [ESP-IDF 5.5.3 USB Serial/JTAG fixed function](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-guides/usb-serial-jtag-console.html)
- [whisper.cpp inference server](https://github.com/ggml-org/whisper.cpp/blob/master/examples/server/README.md)
- [FunASR WebSocket protocol](https://github.com/modelscope/FunASR/blob/main/runtime/docs/websocket_protocol.md)

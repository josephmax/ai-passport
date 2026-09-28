**English** · [简体中文](2026-09-26-voice-input-feasibility.zh_CN.md)

# Voice input feasibility: microphone pendant and desktop input

## Scope and conclusion

Requested scope: cross-platform desktop dictation, macOS first. The pendant captures microphone audio and sends three-button actions; the computer receives audio, runs recognition, and inserts text or mapped navigation/confirmation keys into the focused application. This is feasible as an optional exclusive mode. This document is research and an implementation proposal; no voice firmware, desktop receiver, microphone accuracy, or runtime memory headroom has been validated yet.

The existing ES8311/I2S capture path supports 16 kHz, 16-bit mono PCM through `bsp_audio_read()`. The ESP32-C3 USB peripheral is fixed-function Serial/JTAG, not programmable USB Audio or HID. Consequently the existing USB connector cannot become a standard USB microphone/keyboard by adding TinyUSB. Wi-Fi with a desktop companion is the first transport; a framed USB serial transport is a possible later alternative and must separate binary audio from console logs. Bluetooth LE is not Bluetooth Classic headset audio; adding BLE HID while retaining Wi-Fi adds another radio/lifecycle integration and does not solve microphone transport.

## Proposed first increment

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

## Open-source reuse

| Project | Useful capability | Integration work still required |
| --- | --- | --- |
| [whisper.cpp](https://github.com/ggml-org/whisper.cpp) | Local multilingual recognition, Apple acceleration, HTTP server | Receive ESP PCM, form the inference request, manage model lifetime, input bridge |
| [Handy](https://github.com/cjpais/Handy) | Cross-platform offline dictation, desktop paste and shortcut handling | Documented CLI toggles local recording; it is not a documented ESP PCM ingest API. Extend its audio source or reuse its input approach; verify the chosen release |
| [FunASR](https://github.com/modelscope/FunASR) | Chinese streaming recognition, punctuation, WebSocket protocol | Select a streaming model/runtime; heavier deployment; model weights have separate licenses |

Recommendation: first use multilingual Whisper small/base on the computer and final text after stopping. Avoid `.en` models for Chinese. Use a FunASR streaming adapter later if measured Chinese accuracy or interactive latency warrants it. Do not claim any of these projects is a ready-made network microphone plus three-button input device.

## Cost and resource budget

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

## Acceptance before enabling the feature

1. Record baseline free internal heap, minimum free heap, largest internal block, DMA heap, and stacks in the existing firmware. Repeat while connecting, capturing, stopping, and returning Home. Measure firmware Flash size.
2. Verify a fixed buffer never grows with utterance length; handle slow receiver, queue overflow, Wi-Fi disconnect, receiver exit, malformed frames, cancellation, and duplicate button messages without unintended keys.
3. Run at least 30 minutes and 100 start/stop cycles with the existing pet/focus/settings state preserved. Check codec ownership, no reboot/watchdog, no task/buffer leak, and complete stream teardown before sleep.
4. Measure audio clipping/noise and transcription error on Chinese, English, mixed technical terms, normal distance, and background noise; compare models on the same recordings. Measure end-to-text latency and active current.
5. Test Mac text editor, browser input, terminal, focus change, clipboard restoration, and permission denial. Then test Windows and Linux separately. Verify arrows/Enter/Backspace and configuration changes; Enter must happen only on its mapped event.

## Primary references

- [Board audio and memory constraints](../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md)
- [ESP-IDF 5.5.3 USB Serial/JTAG fixed function](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-guides/usb-serial-jtag-console.html)
- [whisper.cpp inference server](https://github.com/ggml-org/whisper.cpp/blob/master/examples/server/README.md)
- [FunASR WebSocket protocol](https://github.com/modelscope/FunASR/blob/main/runtime/docs/websocket_protocol.md)

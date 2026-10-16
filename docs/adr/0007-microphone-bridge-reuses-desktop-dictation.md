**English** · [简体中文](0007-microphone-bridge-reuses-desktop-dictation.zh_CN.md)

# 0007 — Deliver a microphone stream; reuse desktop dictation instead of building an input method

- Date: 2026-10-14
- Status: BLE/macOS audio bridge implemented 2026-10-15; Typeless acceptance pending
- Supersedes: the Wi-Fi WebSocket, built-in whisper.cpp/FunASR inference, and text/key insertion proposal in the [2026-09-26 voice report](../specs/2026-09-26-voice-input-feasibility.md)

The pendant's role is a microphone, usable by the user's existing voice-input software. Deliver independently framed ADPCM over the BLE connection selected in [ADR 0006](0006-ble-companion-without-device-networking.md). A macOS companion receives and buffers audio, then routes it through a system virtual microphone; Typeless performs recognition, semantic cleanup, and text insertion. Use an independently installed BlackHole 2ch loopback device for the prototype, rather than implementing a custom audio driver. Use the desktop dictation shortcut initially; device-triggered shortcuts are deferred.

This replaces an unnecessarily large MVP containing model management, speech recognition, clipboard handling, focus tracking, and synthetic navigation keys. The firmware stores no recordings; bounded RAM queues are transport buffers, not an utterance archive. The bridge needs no ASR/LLM model or inference API key. Keep the audio endpoint independent of Typeless so other microphone-consuming software can use it.

Typeless is a cloud service, not a local model supplied by this project. It sends audio and limited context to cloud processing; zero retention/local history are not offline operation. Subscription and computer internet access are separate prerequisites, with costs documented in the [updated voice report](../specs/2026-09-26-voice-input-feasibility.md). If offline dictation later becomes a requirement, an independently scoped local ASR and optional text-cleanup adapter can consume the same audio source.

The cost of this boundary is installing and maintaining a desktop bridge plus a virtual audio device, and accepting a third-party dictation dependency. Virtual-device compatibility, mute behavior, first-syllable capture, audio quality, latency, and cloud reachability must be tested. BlackHole's GPLv3/commercial licensing must be resolved before embedding or redistributing it in a product; independent prototype installation is not a decision to bundle its code.

Sources checked 2026-10-14: [Typeless microphone selection](https://www.typeless.com/help/quickstart/settings), [Typeless cloud processing](https://www.typeless.com/data-controls), [BlackHole routing and licensing](https://github.com/ExistentialAudio/BlackHole).

## 2026-10-15 transport revision

Implement independent IMA ADPCM blocks first: 320 samples at 16 kHz, 172 bytes, 20 ms per block, decoded back to PCM on the Mac. This reverses PCM-first to reduce bandwidth and fit one notification per frame; it is not evidence of measured PCM failure. One audio task owns both effects and capture. The bounded Mac jitter queue feeds separately installed BlackHole; existing Typeless still owns recognition/AI cleanup/insertion. No GPL remote-project sources were copied and no Whisper runtime was added.

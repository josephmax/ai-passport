**English** · [简体中文](0001-usage-proxy-holds-credentials.zh_CN.md)

# 0001 — The local service holds all account credentials; the device holds none and pulls hourly

Status updated 2026-10-14: **partially superseded** by [ADR 0006](0006-ble-companion-without-device-networking.md). Host-only provider credentials remain accepted; Wi-Fi/HTTP hourly pull and rejection of a BLE computer companion are replaced. [ADR 0005](0005-separate-usage-quota-and-billing.md) already refines the reading semantics. The original decision below is preserved as history, not the next MVP's transport contract.

The device is a losable wearable, and consumer subscription quotas (Claude
weekly/5-hour limits, etc.) have no official device-facing query API; the
phone config portal also needs to manage several Coding accounts. Decision:
self-host a **local service** on the user's own computer/NAS. The collection
side reuses open source tooling where possible (a ccusage-style reader over
Claude Code's local logs; GLM/DeepSeek via their official balance APIs) and
exposes one unified usage-snapshot endpoint to the device. All vendor
credentials stay server-side; the device stores only the service address and
a revocable device token, and pulls a snapshot once per hour after joining
the network (weather rides along in the same pull). Rejected alternatives:
the device calling vendor APIs directly (keys in plaintext NVS; a lost device
leaks them); a BLE companion on the PC (the dashboard dies away from the
computer); a cloud proxy (public attack surface and operational burden for no
benefit).

Cost: one small long-running service. In exchange: a clean security boundary,
all collection complexity isolated server-side, and switching vendors or
collectors never requires reflashing the device.

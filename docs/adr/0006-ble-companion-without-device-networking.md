**English** · [简体中文](0006-ble-companion-without-device-networking.zh_CN.md)

# 0006 — Use a BLE computer companion; do not provision the pendant onto a network

- Date: 2026-10-14
- Status: firmware and macOS companion implemented 2026-10-15; hardware acceptance pending
- Supersedes: the device transport, hourly pull, and rejection of a BLE companion in [ADR 0001](0001-usage-proxy-holds-credentials.md); the HTTPS delivery assumption in [ADR 0004](0004-asset-pipeline-server-side-transcode.md)
- Implementation contract: [BLE companion MVP](../specs/2026-10-14-ble-companion-mvp.md)

Office networks may refuse untrusted devices, while the user's computer can run a companion and use Bluetooth and USB. Use one authenticated BLE connection for usage snapshots, settings, device status, and microphone audio. The pendant is a BLE peripheral; the computer is the central and owns collection, internet access, and configuration. The target has no SSID/password/service-IP entry, SoftAP onboarding, or device HTTP client. USB remains a development/debug channel, not a required user workflow.

The previous decision rejected BLE because a dashboard would depend on a nearby computer. We now accept that limitation: when disconnected, the pendant keeps its last snapshot and local focus/XP functions. The previous LAN design also required a reachable running service; it did not guarantee autonomous fresh data away from that service. Direct BLE solves the actual office-access constraint and supplies the microphone transport with the same connection.

Provider credentials remain on the host, and the existing snapshot schema, local collectors, asset transcoding, and persistent focus/XP data remain reusable. The first increment uses built-in/current assets; BLE bundle transfer is deferred and must preserve APB1, bounded writes, validation, and atomic installation. Service-side HTTP may remain on loopback for the existing portal and bridge; it is no longer the pendant's protocol.

## 2026-10-15 identity and multi-host revision

The user now requires cross-endpoint identity isolation. Remember up to four authenticated host associations with one selected source. The pendant uses its factory eFuse MAC; each service data directory uses an independent UUID bound to the authenticated BLE peer. New associations and source takeovers require pendant confirmation. Snapshots replace a source rather than summing computers. This supersedes the earlier single-bound-pair assumption; the implementation contract records wire details and rollback identity.

## Alternatives and consequences

- SoftAP or a shared hotspot preserves more HTTP code but adds network switching or a common-network prerequisite. Standard Wi-Fi Direct is not supported by Espressif's current stack. Neither is the accepted onboarding path.
- ESP-NOW requires an ESP receiver/adapter for the ordinary desktop path; it is deferred because the MVP should not need additional hardware.
- NAT traversal requires internet access first and public coordination/relay infrastructure. It cannot help a pendant that cannot join a network; remote connectivity is deferred.
- USB audio data through the fixed serial interface is possible with a bridge, but is wired. Native USB Audio enumeration is not available on ESP32-C3.
- Fresh data and microphone use require a running nearby companion. BLE throughput, host interoperability, RAM, RF interference, and battery life remain hardware acceptance gates; no throughput or endurance is guaranteed by this decision.
- Remove only this application's network setup flow during implementation. Preserve upstream Wi-Fi/BLE demos and hardware documentation. Do not erase NVS or rewrite focus/XP/assets as part of transport migration.

Primary limits: [Wi-Fi Direct FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/wifi.html#do-espressif-chips-support-wi-fi-direct-wi-fi-p2p), [USB Serial/JTAG](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-guides/usb-serial-jtag-console.html).

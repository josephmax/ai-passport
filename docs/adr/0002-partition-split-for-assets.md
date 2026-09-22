**English** · [简体中文](0002-partition-split-for-assets.zh_CN.md)

# 0002 — Re-cut partitions: 3 MB firmware + 5 MB LittleFS asset partition

The upstream baseline gives the whole 8 MB Flash to the `factory` app
partition (0x7F0000) with no data partition. Pet assets must be replaceable
(uploaded and swapped from the config portal), which requires a persistent
device-side writable store. Decision: resize to `factory` at 0x2F0000
(~3 MB — still ample with the Chinese font subset and default assets) plus a
new `assets` LittleFS data partition at 0x500000 (5 MB, roughly 3–4 complete
asset bundles). Default assets are embedded in firmware and written into the
assets partition on first boot or whenever its contents fail validation.

Irreversibility: changing the partition layout erases existing partition data
(harmless for a fresh flash); any later resize loses data again, so the sizes
are chosen once, deliberately.

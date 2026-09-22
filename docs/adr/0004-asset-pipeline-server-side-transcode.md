**English** · [简体中文](0004-asset-pipeline-server-side-transcode.zh_CN.md)

# 0004 — Asset path: the device only downloads; PNG transcoding happens in the config portal

Pet/map/decoration art must be user-replaceable. Decision on the pipeline:
the user uploads PNGs in the config portal (web) → the **server** transcodes
them to the device-native format (RGB565 raw frames — 64×64 characters,
240×160 map strip, 24×24 decorations — plus a frame-table manifest JSON) →
publishes a versioned bundle → the device notices the new version on its
hourly sync and streams it over HTTPS in bounded chunks into LittleFS
(bounded writes, never the whole bundle in RAM). The device does no PNG
decoding and hosts no upload endpoint. Rejected: decoding PNG on-device (no
PSRAM; decode buffers fight LVGL for heap, a known community trap); direct
SoftAP upload (a second transport path plus on-device transcoding for little
gain).

Cost: changing assets always transits the local service. In exchange: a
minimal device implementation (one HTTPS download plus sequential file
writes) and transcoding logic centralized on the server, where the full tool
chain makes size/frame validation easy.

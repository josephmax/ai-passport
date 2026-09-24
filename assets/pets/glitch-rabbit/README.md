# Glitch Rabbit — key pixel sprite

<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

Approved key asset (2026-09-23) for the pet page character: the standing
front-facing base frame at 64×64, drawn to match the official 2D artwork of
the character (lavender body, coral-red overalls, cream wedge in each dome
eye, full row of blocky teeth, uniform dark-brown outline). The style
reference image is described here on purpose and **not committed**.

## Files

- `base-64.png` — approved base frame (64×64 RGBA, 11 colors).
- `gen_sprite.py` — parametric generator that produces `base-64.png`
  (plus `preview_4x.png` and `face_zoom_6x.png` for review). All animation
  frames for the pet actions must derive from this script so the face and
  palette stay consistent.
- `gen_frames.py` — generates run, fight, sleep, and victory frames from the
  same face and palette. `frames/<action>/frame-*.png` are the portable PNG
  source frames; strips and GIFs are visual review aids.
- `validate_frames.py` — checks frame size, binary alpha, ground contact,
  palette, and allowed frame counts before these PNGs enter the asset pipeline.

## Hard constraints (device contract)

- Exactly **64×64** RGBA PNG with binary alpha (portal and pipeline reject
  other sizes; see `service/src/assets/pipeline.ts`).
- **Feet on the bottom row** of the canvas: the device anchors the sprite's
  bottom edge to the ground line and renders it at 2× nearest-neighbor
  (`main/ui/ui_pet.c`, `PET_SCALE`), so every frame of every action must
  keep the feet on the bottom row or the pet will float or sink.
- Time-of-day looks and weather come from runtime tinting and overlay
  sprites — never bake variants into action frames (spec §5.1).

## Frame-count rules (server-enforced)

run/fight 4–8 frames, sleep 2–4 frames, victory 1–4 frames, 1–10 fps per
action. Frames are uploaded as PNG sequences via the config portal and
transcoded server-side to RGB565A8, or baked into the default bundle under
`main/assets_default/` for factory images.

## Regenerate

```bash
python3 gen_sprite.py
python3 gen_frames.py
python3 validate_frames.py
```

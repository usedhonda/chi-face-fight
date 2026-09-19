# Chi animation frame pipeline

`prepare_animation.py` turns an explicitly ordered list of transparent PNGs into
deterministic 80x96 frames for the ESP32. It uses one scale factor for the
whole sequence, horizontally centers each visible bounding box, and pins every
frame to a stable action baseline.

Requirements: Python 3 and Pillow. Inputs are never modified. The output
directory must be new or empty, so reruns cannot silently replace an accepted
asset.

```sh
python3 assets/prepare_animation.py \
  --action kick \
  --name chi-kick-v1 \
  --output-dir assets/build/chi-kick-v1 \
  frame-01.png frame-02.png frame-03.png
```

Pass frames in playback order; shell glob order is not normalized by the tool.
Use `--action kick`, `hit`, or `victory`. Kick and hit contact row 95 (the last
canvas row); victory defaults to row 94 to retain one transparent row below a
celebratory pose. `--baseline` overrides the preset when art direction calls
for a different stable contact row.

For a transparent sprite sheet, provide the expected frame count instead of
positional files:

```sh
python3 assets/prepare_animation.py \
  --sheet-input assets/game-actions-v1/chi-kick-sheet-v1.png \
  --expected-frames 6 \
  --action kick \
  --name chi-kick-sheet-v1 \
  --output-dir assets/build/chi-kick-sheet-v1
```

Sheet mode finds alpha-connected foreground components, keeps character-sized
components, orders them left-to-right, removes every disconnected spill pixel,
and crops each retained character with transparent padding before using the
same common-scale normalization. It does not split the image into equal grid
cells, so wide action poses remain intact. Exactly `--expected-frames`
prominent components must be present; an extra or missing character-sized
component is treated as ambiguous and fails rather than mixing frames.
`--sheet-crop-padding` controls extraction padding and defaults to 2 pixels.

Each run emits numbered normalized PNGs, a horizontal RGBA PNG atlas, a raw
frame-major RGBA8888 file, a checkerboard contact sheet with the baseline in
red, and JSON validation metrics. For `N` frames, raw output is exactly
`N * 80 * 96 * 4` bytes. Metrics record source/final bounding boxes, contact
rows, pixel counts, common scale, and byte count. Missing transparency, empty
frames, clipping, bad ranges, and a non-empty output directory fail closed.

Inspect the contact sheet before promoting generated files. Keep generated
build directories versioned by action/name; promotion into firmware arrays is
a separate reviewed step.

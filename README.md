# Bloub on an ESP32-S3 AMOLED

A port of [jeremy-prt/bloub](https://github.com/jeremy-prt/bloub) — an animated
"blob" character whose every constant was measured frame-by-frame off a
reference video — to the Waveshare ESP32-S3-Touch-AMOLED-1.75 (466x466 round
AMOLED), rendered white-on-black.

Thirteen states cycle on input: **idle, thinking, wink, wide, alert, notify,
sleep, egg, hexagon, play, orbit, burst, comet**. Tap the touchscreen or press
**BOOT** to advance. Each state loops its own animation, and states cross-fade
into one another.

## Hardware

Waveshare ESP32-S3-Touch-AMOLED-1.75: CO5300 QSPI AMOLED, CST9217 capacitive
touch, ESP32-S3R8. Pins live in `main/display.c`, `main/touch.c` and
`main/boot_button.c`.

## Build & flash

```bash
. ~/esp/esp-idf/export.sh
cd esp32-robot-face
idf.py -p /dev/cu.usbmodem<NNNN> build flash monitor
```

Find the port with `ls /dev/cu.*` while the board is plugged in. If the port
appears and disappears in a loop, hold **BOOT** while plugging the cable in to
force ROM download mode.

## What was ported, and what wasn't

Carried over faithfully, because they are measurements rather than settings:
the 64-sample radial profiles for the egg/hexagon/triangle bodies, the exact
eye geometry and the sphere-tangent projection that gives them their volume
(the eyes lean `\\`, not `//`), the blink schedule and its PRNG, the gaze
drift, every state's timings and easings, and the orbit/swoosh/comet ring
parameters.

Deliberately left out: the customiser's shape and expression overrides
(`baseBody`/`baseFace` are inert without it), pointer-driven gaze (`Look` —
there is no mouse), and the export/GIF/video paths.

Changed on purpose: the palette is inverted to white-on-black to suit an
AMOLED, where black costs no power; the ring/swoosh/comet accents keep their
original hues. The notification pastille is green rather than bloub's blue.
The `exclaim` state was dropped.

## How it's made fast

The whole catalogue runs between 35 and 450 fps on one core. The things that
mattered, in order of how much they bought:

- **The body is a filled polygon, not a per-pixel test.** bloub's `toPoints`
  is ported verbatim and the resulting 64-gon is scanline-filled: per row a
  handful of edge crossings and one flat 32-bit span write. The first version
  tested every pixel with an `atan2f`/`sqrtf` against the radial profile — the
  same picture, ~66k transcendental calls a frame, and unusably slow.
- **Eyes are holes, punched not painted.** The body fills first, then the eye
  interiors are erased back to the background over their own small bounding
  boxes. That turned ~66k eye tests per frame (one per body pixel) into ~4k,
  and it is also what clips an eye against the silhouette for free.
- **Band buffers.** A full 466x464 frame does not fit internal SRAM, so the
  screen goes out one 16-row band at a time, double-buffered, drawing the next
  band while the previous one is in flight.
- **Only the rows that can change.** Each frame computes what its content
  actually reaches — body, dots, teardrop, pastille, and each arc's own
  ellipse bound — and repaints only those bands, unioned with the previous
  frame's so vacated bands get blanked exactly once. Sleep's bouncing dot
  touches 4 bands of 29.
- **Arc segments narrow per row.** A diagonal stroke fills a sliver of its
  bounding box; solving the segment for the rows it crosses cuts the tested
  area by about three times.
- **Colours are panel-ordered once, at the point they are built.** This panel
  takes RGB565 byte-swapped. White and black are palindromes, so the bug hid
  until a saturated colour appeared and rendered orange instead of blue.
  Fades are baked into a shape's colour once per frame, never per pixel.

There is an fps line on the serial log every two seconds, with the state, the
bands touched and the arc-segment count, so any future change here can be
measured rather than guessed at.

## Layout

- `main/bloub_states.c` — the state catalogue: one `pose()` per state, plus
  the cross-fade between them. Start here.
- `main/bloub_engine.c` — the render pipeline and the state machine.
- `main/bloub_shapes.c` — silhouettes, the scanline polygon, capsules.
- `main/bloub_face.c` — eyes on a sphere, blink schedule, gaze drift.
- `main/bloub_decor.c` — rings, swoosh, comet ribbons, burst particles.
- `main/display.c` — CO5300 bring-up and the band-buffer DMA pipeline.
- `main/touch.c`, `main/boot_button.c` — the two inputs.
- `main/config.h` — geometry, colours, brightness.

## Credits

All of the character design and animation — the blob's shapes, expressions,
timings, easings, and motion — comes from **[jeremy-prt/bloub](https://github.com/jeremy-prt/bloub)**.
This project is a port of that work to ESP32-S3 hardware; the constants that
give every state its feel were measured from and carried over faithfully from
the original. Full credit and thanks to [Jeremy](https://github.com/jeremy-prt)
for creating Bloub.

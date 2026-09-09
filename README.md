# Bloub on an ESP32-S3 ST7789 LCD

A port of [jeremy-prt/bloub](https://github.com/jeremy-prt/bloub) to an
ESP32-S3 board with a 240x240 ST7789-compatible SPI LCD, with the original
coloured ring, swoosh and comet accents.

The avatar has four independent axes, the same four the original exposes:

| Axis | Count | What it is |
| --- | ---: | --- |
| **state** | 14 (+1) | What the avatar is doing: idle, thinking, wink, wide, alert, notify, exclaim, sleep, egg, hexagon, play, orbit, burst, comet. Plus `swirl`, an interface transition kept outside the cycle. |
| **expression** | 16 | The resting face: neutre, attentif, surpris, excite, heureux, hilare, colere, triste, effraye, mefiant, confus, curieux, fier, timide, blase, somnolent. |
| **shape** | 8 | The resting body: cercle, galet, squircle, capsule, triangle, hexagone, nuage, goutte. |
| **colour** | 12 | encre, brun, rouge, orange, ambre, vert, turquoise, bleu, violet, rose, gris, creme. |

Shape and expression are not global overrides. A state that draws its own
silhouette -- the "!", the dots, the egg, the spinning triangle -- keeps it,
because there the silhouette *is* the animation. Only `idle`, `wink`, `wide`,
`notify` and `swirl` take a shape, and only `idle` and `swirl` take an
expression. That is the original's rule, not a limitation of the port.

The demo firmware walks all four at once, on pairwise-coprime periods (4, 7, 11
and 13 seconds) so the combinations keep changing.

## Hardware

The LCD configuration is taken from the working sibling project
`../20_Camera`:

| Signal | ESP32-S3 GPIO |
| --- | ---: |
| SPI SCLK | 12 |
| SPI MOSI | 11 |
| SPI MISO | 13 |
| LCD CS | 48 |
| LCD DC | 47 |
| LCD RST | 21 |
| LCD backlight | 40 |

The display uses SPI2 at 60 MHz in mode 0. The backlight is active high.
Camera, Wi-Fi, touch and button input are not used by this firmware.

## Build and flash

This project is configured and tested with ESP-IDF v6.0.2.

```bash
. /home/sqhh99/esp32/esp-idf/export.sh
cd esp32-robot-face
idf.py build
idf.py -p <PORT> flash monitor
```

The serial log prints the frame rate, the four active axes, the updated band
range and the arc segment count every two seconds.

### Regenerating the derived tables

Two source files are generated, not written:

```bash
python3 tools/gen_bloub_tables.py            # rewrite both .c files
python3 tools/gen_bloub_tables.py --check    # verify, write nothing
python3 tools/gen_bloub_tables.py --preview sheet.png
```

`bloub_skins.c` holds the eight shape profiles, built analytically from
superellipses, unions of discs and rounded polygons. `bloub_eyefit.c` holds the
eye-fit offsets described below. Only the tool writes them; it needs nothing but
Python 3.

## Rendering

The project uses a small purpose-built RGB565 rasterizer rather than LVGL:

- The 64-sample body silhouette is converted to a polygon and scanline-filled.
- Eyes are erased from the body over their small bounding boxes.
- The 240x240 display is sent as 15 bands of 16 rows.
- Two internal-SRAM buffers overlap rendering with SPI DMA transfers.
- Only bands touched by the current or previous frame are refreshed.
- Orbit and ribbon curves are rasterized as short capsule segments.

The renderer keeps colours in the byte order expected by the panel, so fades
are composed before the final RGB565 byte swap.

The body colour and the page colour are both configurable. `encre` (`#0a0a0c`)
is the original's default, but the original draws on a light page: against the
black background this firmware defaults to it is invisible. Set
`FACE_BACKGROUND` in `main/config.h` to a light value to use it.

### Fitting the eyes to a shape

The eyes live on a sphere, and a pro-rata of the silhouette's real radius sticks
them back onto its outline. That places their *centre* correctly and is not
enough: an eye has a size, and the margin in front of the edge gets multiplied
by the same factor, so a shape that is narrow in the eye's direction pushes it
straight through -- visibly, on the capsule, the triangle, the cloud and the
droplet.

The fix is a common translation applied to both eyes, hence an isometry:
spacing, sizes and leans survive to the pixel, the face just sits a little lower
on a body with no room up there. It is solved **once, offline**, into a table of
8 shapes x 37 columns. Solving it per frame is the trap: everything the solver
reads moves at frame rate -- the gaze drift, the expression mid-morph, which
edge is nearest -- and the original measured and rejected seven such variants,
every one of which made the eyes tremble.

## Reusing the face component

The animation and rasterizer live in the display-independent
`components/grok_face` component. Its public `grok_face.h` API accepts the
display dimensions, face geometry and two callbacks for acquiring and
flushing RGB565 band buffers. It contains no LCD controller, GPIO or playback
interval assumptions.

To use it in another ESP-IDF project, copy `components/grok_face`, provide
the two display callbacks, call `grok_face_render_frame()` continuously and
drive the four axes with `grok_face_set_state()` / `grok_face_next_state()`,
`grok_face_set_expression()`, `grok_face_set_shape()` and
`grok_face_set_color()`. `grok_face_set_look()` aims the gaze at an absolute
yaw and pitch, for a sensor or a touch input. See
[`components/grok_face/README.md`](components/grok_face/README.md) for the
complete buffer contract and integration example.

## Layout

- `components/grok_face` - reusable animation and RGB565 rasterizer
- `tools/gen_bloub_tables.py` - generates the shape and eye-fit tables
- `main/display.c` - ST7789 initialization and band-buffered SPI DMA
- `main/main.c` - component wiring and the four-axis demo cycle
- `main/config.h` - display geometry and playback settings

## Credits

All character design and animation - shapes, expressions, colours, timings,
easings and motion - comes from
[jeremy-prt/bloub](https://github.com/jeremy-prt/bloub). This project ports that
work to ESP32-S3 hardware.

Its numeric constants are measurements taken frame by frame off a reference
video, not settings. Rounding them or replacing them with tidier-looking values
breaks the resemblance, which is the only thing this port is trying to achieve.

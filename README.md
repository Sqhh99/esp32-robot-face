# Bloub on an ESP32-S3 ST7789 LCD

A port of [jeremy-prt/bloub](https://github.com/jeremy-prt/bloub) to an
ESP32-S3 board with a 240x240 ST7789-compatible SPI LCD. The character is
rendered white-on-black, with the original coloured ring, swoosh and comet
accents.

Thirteen states play automatically: **idle, thinking, wink, wide, alert,
notify, sleep, egg, hexagon, play, orbit, burst, comet**. Each state is shown
for four seconds, including its transition, and the sequence then wraps back
to idle.

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

The serial log prints the active state, frame rate, updated band range and arc
segment count every two seconds.

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

## Reusing the face component

The animation and rasterizer live in the display-independent
`components/grok_face` component. Its public `grok_face.h` API accepts the
display dimensions, face geometry and two callbacks for acquiring and
flushing RGB565 band buffers. It contains no LCD controller, GPIO or playback
interval assumptions.

To use it in another ESP-IDF project, copy `components/grok_face`, provide
the two display callbacks, call `grok_face_render_frame()` continuously and
select expressions through `grok_face_set_expression()` or
`grok_face_next_expression()`. See
[`components/grok_face/README.md`](components/grok_face/README.md) for the
complete buffer contract and integration example.

## Layout

- `components/grok_face` - reusable animation and RGB565 rasterizer
- `main/display.c` - ST7789 initialization and band-buffered SPI DMA
- `main/main.c` - component wiring and four-second automatic state cycling
- `main/config.h` - display geometry and playback settings

## Credits

All character design and animation - shapes, expressions, timings, easings and
motion - comes from [jeremy-prt/bloub](https://github.com/jeremy-prt/bloub).
This project ports that work to ESP32-S3 hardware.

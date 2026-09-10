#pragma once

// Every tunable in this project lives here.

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

// The board's ST7789 LCD is 240x240.
#define LCD_H_RES 240
#define LCD_V_RES 240

// The renderer never holds a whole frame. It draws one horizontal band at a
// time into internal SRAM and DMAs it out while drawing the next one, which
// keeps the framebuffer off PSRAM entirely.
#define BAND_ROWS 16
#define BAND_COUNT (LCD_V_RES / BAND_ROWS)

// ---------------------------------------------------------------------------
// Bloub geometry
// ---------------------------------------------------------------------------

// Screen-space centre of the ball. Slightly above the panel's own centre.
#define BALL_CX ((float)(LCD_H_RES / 2))
#define BALL_CY ((float)(LCD_V_RES / 2) - 3.0f)

// Resting ball radius, in pixels. The largest reach anything gets to is the
// orbit rings, semi-major axis up to ~1.42x plus a stroke half-width — sized
// so that stays inside the 240x240 panel with margin to spare.
#define BALL_R 75.0f

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

// Active-low buttons. These pins overlap the camera D4-D7 bus used by the
// sibling camera example, but that interface is not enabled in this project.
#define KEY1_GPIO 15
#define KEY2_GPIO 16
#define KEY3_GPIO 17
#define KEY4_GPIO 18
#define KEY_DEBOUNCE_MS 20

// Page colour behind the avatar, native RGB565. Black suits the panel; note
// that GROK_FACE_COLOR_ENCRE (#0a0a0c) is meant for a light page and will be
// invisible against this one.
#define FACE_BACKGROUND 0x0000

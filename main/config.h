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
// Playback
// ---------------------------------------------------------------------------

#define STATE_HOLD_MS 4000

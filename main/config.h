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

// The demo walks all four axes at once. The four periods are pairwise coprime
// so the combinations keep changing instead of settling into a short pattern:
// it takes 4*7*11*13 seconds to repeat.
//
// Shape and expression only SHOW on the states that carry the resting body and
// the resting face (idle, wink, wide, notify -- and idle alone for the face).
// Everywhere else the silhouette is the animation, so it is left alone. Seeing
// them change only some of the time is the correct behaviour, not a fault.
#define STATE_HOLD_MS 4000
#define EXPR_HOLD_MS 7000
#define SHAPE_HOLD_MS 11000
#define COLOR_HOLD_MS 13000

// Page colour behind the avatar, native RGB565. Black suits the panel; note
// that GROK_FACE_COLOR_ENCRE (#0a0a0c) is meant for a light page and will be
// invisible against this one.
#define FACE_BACKGROUND 0x0000

#pragma once

// Every tunable in this project lives here.

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

// Waveshare ESP32-S3-Touch-AMOLED-1.75: the panel is a 466x466 round AMOLED.
#define LCD_H_RES 466
#define LCD_V_RES 464 // trimmed by 2 so BAND_ROWS divides it evenly

// The renderer never holds a whole frame. It draws one horizontal band at a
// time into internal SRAM and DMAs it out while drawing the next one, which
// keeps the framebuffer off PSRAM entirely.
#define BAND_ROWS 16
#define BAND_COUNT (LCD_V_RES / BAND_ROWS)

#define DISPLAY_BRIGHTNESS 220 // 0..255, written to panel register 0x51

// ---------------------------------------------------------------------------
// Bloub geometry
// ---------------------------------------------------------------------------

// Screen-space centre of the ball. Slightly above the panel's own centre,
// matching the composition of the reference video (and leaving a little more
// room below for the rings' lower sweep than above).
#define BALL_CX ((float)(LCD_H_RES / 2))
#define BALL_CY ((float)(LCD_V_RES / 2) - 6.0f)

// Resting ball radius, in pixels. The largest reach anything gets to is the
// orbit rings, semi-major axis up to ~1.42x plus a stroke half-width — sized
// so that stays comfortably inside the round panel's inscribed circle
// (radius LCD_H_RES/2 = 233) with margin to spare.
#define BALL_R 145.0f

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------

// Colours reaching a band buffer must be panel-ordered (see bloub_panel_swap).
// White and black are palindromes, so these two need no swap.
#define COLOR_BG 0x0000
#define COLOR_FG 0xFFFF // body; the eye holes reveal COLOR_BG through it

// bloub's notification pastille is #2496e8 blue; green here by request.
#define NOTIF_R8 46
#define NOTIF_G8 232
#define NOTIF_B8 106

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

#define INPUT_POLL_PERIOD_MS 20

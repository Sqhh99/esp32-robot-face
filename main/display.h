#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// Owns the ST7789 LCD panel and the pair of band buffers that feed it.
//
// The screen is pushed out one horizontal band at a time. Callers render into
// the buffer returned by display_acquire_band() and hand it to
// display_flush_rows(), which starts a DMA transfer and returns immediately.
// Acquiring blocks only if both buffers are still in flight, so drawing one
// band overlaps with transmitting the previous one.

esp_err_t display_init(void);

// Blocks until a band buffer is free, then returns it. BAND_ROWS * LCD_H_RES
// pixels. Buffers rotate per call rather than per band index, because the
// face renderer skips bands the eyes never reach, and two transfers in flight
// must never land in the same buffer.
uint16_t *display_acquire_band(void);

// Queues an asynchronous transfer of one horizontal region. Does not block.
esp_err_t display_flush_rows(int y_start, int row_count, const uint16_t *buffer);

// Controls the board's active-high LCD backlight GPIO.
esp_err_t display_set_backlight(bool on);

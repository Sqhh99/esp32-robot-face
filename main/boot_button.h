#pragma once

#include <stdbool.h>

#include "esp_err.h"

// The BOOT button. It doubles as the ROM download-mode strap at power-on and
// during a reset, but once the app is running it is a free, ordinary input,
// pulled low while held.
//
// Only short presses are reported; nothing in this project needs a
// long-press gesture on BOOT.

esp_err_t boot_button_init(void);

// Call periodically. Returns true exactly once per completed short press.
bool boot_button_take_short_press(void);

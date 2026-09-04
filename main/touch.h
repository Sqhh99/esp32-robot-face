#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

// CST9217 capacitive touch controller (I2C + QSPI reset/int lines), shared
// on the board's common I2C bus.
//
// Only tap detection is needed for this project, not tracking or gestures,
// so this wraps the vendor driver down to a single edge-triggered poll.

esp_err_t touch_init(i2c_master_bus_handle_t bus);

// Call periodically. Returns true exactly once per finger-down event (the
// instant a touch begins), regardless of how long the finger stays down.
bool touch_take_tap(void);

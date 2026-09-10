#pragma once

#include "esp_err.h"

typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_1,
    KEY_EVENT_2,
    KEY_EVENT_3,
    KEY_EVENT_4,
} key_event_t;

/** Configures KEY1-KEY4 as active-low inputs with internal pull-ups. */
esp_err_t keys_init(void);

/** Returns one debounced press event without blocking the render loop. */
key_event_t keys_poll(void);

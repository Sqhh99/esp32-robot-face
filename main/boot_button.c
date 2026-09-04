#include "boot_button.h"

#include "driver/gpio.h"
#include "esp_timer.h"

#define BOOT_BUTTON_GPIO GPIO_NUM_0

// Raw mechanical switch with no debouncing done for us in hardware, so it
// gets a sample-and-agree debounce.
#define DEBOUNCE_SAMPLES 3
#define SHORT_PRESS_MAX_MS 1500

static bool s_stable_pressed;
static int s_agree_count;
static int64_t s_press_started_us;
static bool s_pending_short_press;

esp_err_t boot_button_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

bool boot_button_take_short_press(void)
{
    const bool pressed = gpio_get_level(BOOT_BUTTON_GPIO) == 0;  // active low

    if (pressed == s_stable_pressed) {
        s_agree_count = 0;
    } else if (++s_agree_count >= DEBOUNCE_SAMPLES) {
        s_agree_count = 0;
        s_stable_pressed = pressed;

        if (pressed) {
            s_press_started_us = esp_timer_get_time();
        } else {
            const int64_t held_ms = (esp_timer_get_time() - s_press_started_us) / 1000;
            if (held_ms <= SHORT_PRESS_MAX_MS) {
                s_pending_short_press = true;
            }
        }
    }

    if (s_pending_short_press) {
        s_pending_short_press = false;
        return true;
    }
    return false;
}

#include "keys.h"

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define KEY_COUNT 4
#define KEY_DEBOUNCE_US ((int64_t)KEY_DEBOUNCE_MS * 1000)

typedef struct {
    int stable_level;
    int candidate_level;
    int64_t candidate_since_us;
} key_state_t;

static const gpio_num_t s_key_gpios[KEY_COUNT] = {
    (gpio_num_t)KEY1_GPIO,
    (gpio_num_t)KEY2_GPIO,
    (gpio_num_t)KEY3_GPIO,
    (gpio_num_t)KEY4_GPIO,
};

static key_state_t s_key_states[KEY_COUNT];
static bool s_initialized;

esp_err_t keys_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << KEY1_GPIO) | (1ULL << KEY2_GPIO) |
                        (1ULL << KEY3_GPIO) | (1ULL << KEY4_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    const int64_t now_us = esp_timer_get_time();
    for (int i = 0; i < KEY_COUNT; i++) {
        int level = gpio_get_level(s_key_gpios[i]);
        s_key_states[i] = (key_state_t){
            .stable_level = level,
            .candidate_level = level,
            .candidate_since_us = now_us,
        };
    }
    s_initialized = true;
    return ESP_OK;
}

key_event_t keys_poll(void)
{
    if (!s_initialized) {
        return KEY_EVENT_NONE;
    }

    const int64_t now_us = esp_timer_get_time();
    key_event_t event = KEY_EVENT_NONE;

    for (int i = 0; i < KEY_COUNT; i++) {
        key_state_t *state = &s_key_states[i];
        int level = gpio_get_level(s_key_gpios[i]);

        if (level != state->candidate_level) {
            state->candidate_level = level;
            state->candidate_since_us = now_us;
            continue;
        }
        if (level == state->stable_level ||
            now_us - state->candidate_since_us < KEY_DEBOUNCE_US) {
            continue;
        }

        state->stable_level = level;
        if (level == 0 && event == KEY_EVENT_NONE) {
            event = (key_event_t)(KEY_EVENT_1 + i);
        }
    }

    return event;
}

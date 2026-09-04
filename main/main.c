// Bloub: a 14-expression animated "blob" face on the round AMOLED, ported
// from https://github.com/jeremy-prt/bloub (see bloub_states.h for what was
// and wasn't carried over). Either tapping the touchscreen or pressing the
// BOOT button advances to the next state; each one plays its own looping
// animation until the next input arrives.
//
// Rendering and input polling both run in app_main's own loop. Nothing here
// is CPU-bound enough to need a second task or core: the render loop is
// entirely paced by waiting on band DMA transfers to complete (see
// display_acquire_band), and input is polled at a fixed cadence in the gaps.

#include "bloub_engine.h"
#include "boot_button.h"
#include "config.h"
#include "display.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "touch.h"

#define I2C_PORT I2C_NUM_0
#define I2C_SDA GPIO_NUM_15
#define I2C_SCL GPIO_NUM_14

static const char *TAG = "bloub_face";

static esp_err_t i2c_init(i2c_master_bus_handle_t *out_bus)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, out_bus);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Bloub starting");

    i2c_master_bus_handle_t i2c_bus;
    ESP_ERROR_CHECK(i2c_init(&i2c_bus));

    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(display_set_brightness(DISPLAY_BRIGHTNESS));

    // Touch and the BOOT button are both optional inputs; either one working
    // alone is enough to cycle expressions, so neither failure is fatal.
    if (touch_init(i2c_bus) != ESP_OK) {
        ESP_LOGW(TAG, "continuing without touch input");
    }
    if (boot_button_init() != ESP_OK) {
        ESP_LOGW(TAG, "continuing without the BOOT button");
    }

    bloub_engine_init();

    int64_t last_input_poll_us = esp_timer_get_time();

    for (;;) {
        bloub_engine_render_frame();

        const int64_t now = esp_timer_get_time();
        if (now - last_input_poll_us >= INPUT_POLL_PERIOD_MS * 1000) {
            last_input_poll_us = now;

            bool advance = false;
            if (boot_button_take_short_press()) {
                ESP_LOGI(TAG, "BOOT pressed");
                advance = true;
            }
            if (touch_take_tap()) {
                ESP_LOGI(TAG, "touch tap");
                advance = true;
            }
            if (advance) {
                bloub_engine_next_state();
            }
        }

        // The render loop is already paced by band DMA waits; this just
        // guarantees the idle task gets scheduled so the watchdog stays fed.
        vTaskDelay(1);
    }
}

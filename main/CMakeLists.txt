idf_component_register(
    SRCS
        "main.c"
        "display.c"
        "keys.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES grok_face esp_lcd esp_driver_gpio esp_driver_spi esp_timer
)

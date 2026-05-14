idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_lcd
        esp_psram
        nvs_flash
        vfs
        esp_lvgl_port
        spi_flash
)
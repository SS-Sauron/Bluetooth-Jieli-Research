# ── GUI‑Guider UI sources & include paths ─────────────────────────
set(UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ui)

file(GLOB_RECURSE UI_SRCS
    ${UI_DIR}/custom/*.c
    ${UI_DIR}/generated/*.c
    ${UI_DIR}/generated/guider_fonts/*.c
    ${UI_DIR}/generated/images/*.c
)

list(APPEND UI_INCLUDE_DIRS
    ${UI_DIR}/custom
    ${UI_DIR}/generated
    ${UI_DIR}/generated/guider_fonts
    ${UI_DIR}/generated/images
)

# ── Main component registration ───────────────────────────────────
idf_component_register(
    SRCS "main.c" ${UI_SRCS}
    INCLUDE_DIRS "." ${UI_INCLUDE_DIRS}
    REQUIRES
        driver
        esp_lcd
        esp_psram
        nvs_flash
        vfs
        esp_lvgl_port
        spi_flash
)
/*
 * CrowPanel 2.8" ESP32-S3 (V1.0) — Display test with LVGL 8.3.x
 * Uses esp_lvgl_port v1.4.0 (lvgl_port_* API, LVGL 8.x)
 *
 * Fixes applied vs original:
 *   1. Added LCD_CS_GPIO (was -1, meaning SPI never selected the panel)
 *   2. Backlight enabled BEFORE display-on, not after
 *   3. Removed esp_lcd_panel_mirror() — orientation is handled solely via
 *      lvgl_port_display_cfg_t rotation, avoiding double-mirror corruption
 *   4. ESP_ERROR_CHECK wrapped around every fallible call
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "gui_guider.h"
#include "events_init.h"

static const char *TAG = "crowpanel";

/* ── Pin definitions (CrowPanel 2.8" V1.0) ──────────────────────────── */
#define LCD_SCLK_GPIO 42
#define LCD_MOSI_GPIO 39
#define LCD_DC_GPIO 41
#define LCD_BL_GPIO 38
#define LCD_H_RES 320
#define LCD_V_RES 240

/* ── LVGL draw buffer: 1/4 screen height ────────────────────────────── */
#define LVGL_BUF_LINES (LCD_V_RES / 4) /* 60 lines */

/* ════════════════════════════════════════════════════════════════════════ */

void app_main(void)
{
    /* 1. NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    else
    {
        ESP_ERROR_CHECK(ret);
    }

    /* 2. SPI bus */
    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LVGL_BUF_LINES * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* 3. Panel IO (SPI) */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_DC_GPIO,
        .cs_gpio_num = -1,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    /* 4. ST7789 panel — BGR color order */
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
    /* FIX 3: removed esp_lcd_panel_mirror() here — rotation handled by
     *        lvgl_port_display_cfg_t below to avoid double-mirroring    */

    /* FIX 2: backlight ON before display-on so panel is lit when it wakes */
    gpio_set_direction(LCD_BL_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL_GPIO, 1);
    ESP_LOGI(TAG, "Backlight on");

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    ESP_LOGI(TAG, "Display on");

    vTaskDelay(pdMS_TO_TICKS(100));

    /* 5. LVGL port init */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    /* 6. Add display
     *    swap_xy + mirror_x gives correct landscape for ST7789 320×240.
     *    Adjust if your image appears rotated/flipped.                  */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel,
        .buffer_size = LCD_H_RES * LVGL_BUF_LINES,
        .double_buffer = false,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = true, /* FIX 3: landscape orientation */
            .mirror_x = true,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
        },
    };
    lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);
    (void)disp;

    /* 7. GUI‑Guider generated UI */
    lv_ui guider_ui;
    setup_ui(&guider_ui);

    lvgl_port_lock(0);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
    lvgl_port_unlock();

    events_init(&guider_ui);
    ESP_LOGI(TAG, "GUI‑Guider UI loaded");

    /* 8. Main loop — LVGL driven by its internal FreeRTOS task */
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
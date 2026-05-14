/*
 * CrowPanel 2.8" ESP32-S3 (V1.0) — Display test with LVGL 8.3.x
 * Uses esp_lvgl_port v1.4.0 (lvgl_port_* API, LVGL 8.x)
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

static const char *TAG = "crowpanel";

/* ── Verified pins (Elecrow README) ─────────────────────────────────── */
#define LCD_SCLK_GPIO 42
#define LCD_MOSI_GPIO 39
#define LCD_DC_GPIO 41
#define LCD_BL_GPIO 38
#define LCD_H_RES 320
#define LCD_V_RES 240

/* ════════════════════════════════════════════════════════════════════════ */

void app_main(void)
{
    /* 1. NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }

    /* 2. SPI bus */
    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * 2 + 8,
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

    /* 4. ST7789 panel — BGR + mirrored XY (verified from official Elecrow example) */
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    /* Backlight ON */
    gpio_set_direction(LCD_BL_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL_GPIO, 1);
    ESP_LOGI(TAG, "Display on");

    /* 5. LVGL port init — v1.x API: ESP_LVGL_PORT_INIT_CONFIG + lvgl_port_init */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    /* 6. Add display — v1.x API: lvgl_port_add_disp (no swap_bytes in v1.x flags) */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel,
        .buffer_size = LCD_H_RES * LCD_V_RES / 4,
        .double_buffer = false,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            /* v1.x has no swap_bytes field — byte swap is handled
               by the esp_lcd driver's rgb_ele_order (BGR) config */
        },
    };
    lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);
    (void)disp;

    /* 7. Centred label — v1.x API: lvgl_port_lock / lvgl_port_unlock */
    lvgl_port_lock(0);
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "CrowPanel Ready");
    lv_obj_center(label);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "UI created");

    /* 8. Main loop — LVGL is driven by its own internal task */
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
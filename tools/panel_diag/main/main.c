/*
 * CrowPanel 2.8" ESP32-S3 — Hardware Diagnostics (extended)
 *
 * Verified pins (Elecrow documentation):
 *   Display SPI:  SCLK=42  MOSI=39  MISO=-1  DC=41  CS=-1  RST=-1  BL=38
 *   Touch I2C:    SDA=15   SCL=16
 *   Board:        ESP32-S3-WROOM-1-N16R8
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_psram.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_flash.h"

static const char *TAG = "diag";

/* ── Pin assignments ──────────────────────────────────────────────────── */
#define LCD_SCLK_GPIO 42
#define LCD_MOSI_GPIO 39
#define LCD_MISO_GPIO (-1)
#define LCD_DC_GPIO 41
#define LCD_CS_GPIO (-1)
#define LCD_RST_GPIO (-1)
#define LCD_BL_GPIO 38

#define I2C_SDA_GPIO 15
#define I2C_SCL_GPIO 16

#define TOUCH_RST_GPIO 18
#define TOUCH_INT_GPIO 17

/* ── Display geometry ──────────────────────────────────────────────────── */
#define LCD_H_RES 320
#define LCD_V_RES 240

/* ── Read an ST7789 register (may return 0xFF when MISO is absent) ────── */
static void read_lcd_register(esp_lcd_panel_io_handle_t io, uint8_t reg,
                              uint8_t *dst, size_t len)
{
    esp_lcd_panel_io_tx_param(io, reg, NULL, 0);
    esp_lcd_panel_io_rx_param(io, reg, dst, len);
}

/* ════════════════════════════════════════════════════════════════════════ */

void app_main(void)
{
    /* 1. NVS init */
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
        .miso_io_num = LCD_MISO_GPIO,
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
        .cs_gpio_num = LCD_CS_GPIO,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    /* 4. ST7789 panel handle */
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(
        io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    /* ── Soft‑reset display controller ────────────────────────────────
     * ST7789V datasheet §8.2.1: SWRESET (0x01) resets all registers
     * to factory defaults.  Wait 120 ms for the controller to recover. */
    esp_lcd_panel_io_tx_param(io_handle, 0x01, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_LOGI(TAG, "Display soft‑reset complete (SWRESET 0x01, 150 ms delay)");

    /* ── Read display ID (register 0x04) ──────────────────────────────── */
    uint8_t id[4] = {0};
    read_lcd_register(io_handle, 0x04, id, sizeof(id));
    ESP_LOGI(TAG, "ST7789 ID: %02X %02X %02X %02X  (0xFF = MISO absent, expected on 3-wire SPI)",
             id[0], id[1], id[2], id[3]);

    /* ── Read MADCTL (register 0x36) ───────────────────────────────────── */
    uint8_t madctl = 0;
    read_lcd_register(io_handle, 0x36, &madctl, 1);
    ESP_LOGI(TAG, "MADCTL: 0x%02X  (MX:%d MY:%d MV:%d ML:%d RGB/BGR:%d)  (0xFF = MISO absent)",
             madctl,
             (madctl >> 6) & 1,
             (madctl >> 7) & 1,
             (madctl >> 5) & 1,
             (madctl >> 4) & 1,
             (madctl >> 3) & 1);
    /* ── Interpretive output for display ──────────────────────────── */
    if (id[0] == 0x85 && id[1] == 0x85 && id[2] == 0x85)
    {
        ESP_LOGI(TAG, "  → ST7789V confirmed (all 0x85 is normal for this controller).");
    }
    else if (id[0] == 0xFF && id[1] == 0xFF && id[2] == 0xFF)
    {
        ESP_LOGI(TAG, "  → All 0xFF: MISO is likely disconnected (3‑wire SPI). This is expected on most ESP32 display boards.");
    }
    else if (id[0] == 0x00 && id[1] == 0x93 && id[2] == 0x41)
    {
        ESP_LOGI(TAG, "  → ILI9341 controller detected.");
    }
    else
    {
        ESP_LOGI(TAG, "  → Unknown display controller ID. Check wiring or consult datasheet.");
    }

    /* Interpret MADCTL */
    if (madctl != 0xFF)
    {
        ESP_LOGI(TAG, "  → MADCTL: MX=%d MY=%d (mirror X/Y), MV=%d (row/col swap), ML=%d (refresh dir), RGB/BGR=%d (0=RGB, 1=BGR)",
                 (madctl >> 6) & 1, (madctl >> 7) & 1,
                 (madctl >> 5) & 1, (madctl >> 4) & 1,
                 (madctl >> 3) & 1);
        ESP_LOGI(TAG, "  → If colors look wrong, try swapping RGB/BGR in panel_config.rgb_ele_order.");
        ESP_LOGI(TAG, "  → If image is mirrored/flipped, toggle mirror_x/mirror_y in LVGL rotation config.");
    }
    else
    {
        ESP_LOGI(TAG, "  → MADCTL read returned 0xFF — display may be in sleep mode or MISO absent.");
    }

    /* ── Backlight test (GPIO 38) ──────────────────────────────────────── */
    gpio_set_direction(LCD_BL_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL_GPIO, 1);
    ESP_LOGI(TAG, "Backlight ON (GPIO %d)", LCD_BL_GPIO);

    /* ── Reset FT6336U touch controller ──────────────────────────────── */
    gpio_set_direction(TOUCH_RST_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(TOUCH_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20)); // hold low for 20 ms
    gpio_set_level(TOUCH_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(300)); // wait 300 ms for boot
    ESP_LOGI(TAG, "Touch controller reset complete");

    /* ── I2C bus init ──────────────────────────────────────────────────── */
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true, // ← enable if board lacks external pull‑ups
        },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus));

    /* ── I2C scan (touch controller) ───────────────────────────────────── */
    uint8_t touch_addr = 0;
    const char *touch_type = "none";
    ESP_LOGI(TAG, "I2C bus scan:");
    for (uint8_t addr = 1; addr < 127; addr++)
    {
        esp_err_t err = i2c_master_probe(i2c_bus, addr, pdMS_TO_TICKS(200));
        if (err == ESP_OK)
        {
            touch_addr = addr;
            if (addr == 0x38)
                touch_type = "FT6336U";
            else if (addr == 0x5D)
                touch_type = "GT911";
            else if (addr == 0x14)
                touch_type = "GT911";
            else
                touch_type = "unknown";
            ESP_LOGI(TAG, "  Found device at 0x%02X (%s)", addr, touch_type);
        }
    }

    /* ── Read touch controller registers (FT6336U) ──────────────────── */
    if (touch_addr == 0x38)
    {
        i2c_master_dev_handle_t dev_handle = NULL;
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = touch_addr,
            .scl_speed_hz = 50000, // lower speed for reliability
        };
        ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &dev_handle));

        /* Try reading register 0xA6 (firmware ID) */
        uint8_t fw_id = 0;
        const uint8_t reg_fw = 0xA6;
        esp_err_t err = i2c_master_transmit_receive(
            dev_handle,
            &reg_fw, 1, // write register address
            &fw_id, 1,  // read one byte
            pdMS_TO_TICKS(200));
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "  FT6336U firmware ID: 0x%02X", fw_id);
        }
        else
        {
            ESP_LOGW(TAG, "  Register 0xA6 read failed: %s", esp_err_to_name(err));
        }

        /* Try reading register 0x00 (chip vendor ID) */
        uint8_t vendor_id = 0;
        const uint8_t reg_vendor = 0x00;
        err = i2c_master_transmit_receive(
            dev_handle,
            &reg_vendor, 1,
            &vendor_id, 1,
            pdMS_TO_TICKS(200));
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "  FT6336U vendor ID: 0x%02X", vendor_id);
        }
        else
        {
            ESP_LOGW(TAG, "  Register 0x00 read failed: %s", esp_err_to_name(err));
        }

        /* Try reading register 0x02 (TD_STATUS — touch point count) */
        uint8_t td_status = 0;
        const uint8_t reg_td = 0x02;
        err = i2c_master_transmit_receive(
            dev_handle,
            &reg_td, 1,
            &td_status, 1,
            pdMS_TO_TICKS(200));
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "  Touch points: %d", td_status & 0x0F);
        }
        else
        {
            ESP_LOGW(TAG, "  Register 0x02 read failed: %s", esp_err_to_name(err));
        }

        /* ── Interpretive output for FT6336U ──────────────────────── */
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "  → Touch controller is alive and responding.");
        }
        else
        {
            ESP_LOGI(TAG, "  → Touch controller ACKs its I2C address but register reads failed.");
            ESP_LOGI(TAG, "  → Check: 1) RST pin wired and released? 2) Post‑reset delay ≥ 200ms? 3) I2C pull‑ups present?");
        }
        ESP_LOGI(TAG, "  → If touch still doesn't work, verify INT pin configuration in your firmware.");

        i2c_master_bus_rm_device(dev_handle);
    }
    /* ── Read GT911 product ID (registers 0x8140-0x8143) ──────────── */
    if (touch_addr == 0x5D || touch_addr == 0x14)
    {
        i2c_master_dev_handle_t gt911_dev = NULL;
        i2c_device_config_t gt911_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = touch_addr,
            .scl_speed_hz = 50000,
        };
        ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &gt911_cfg, &gt911_dev));

        /* GT911 product ID is at 16-bit register 0x8140 */
        uint8_t pid_buf[4] = {0};
        uint8_t reg_addr16[2] = {0x81, 0x40}; // big-endian: high byte first
        esp_err_t err = i2c_master_transmit_receive(
            gt911_dev,
            reg_addr16, 2, // write 2-byte register address
            pid_buf, 4,    // read 4 bytes
            pdMS_TO_TICKS(100));
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "  GT911 product ID: %c%c%c%c",
                     pid_buf[0], pid_buf[1], pid_buf[2], pid_buf[3]);
        }
        else
        {
            ESP_LOGW(TAG, "  GT911 product ID read failed: %s", esp_err_to_name(err));
        }
        i2c_master_bus_rm_device(gt911_dev);
    }

    /* ── NEW: Board & system diagnostics ───────────────────────────────── */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    const char *model = "ESP32";
    if (chip_info.model == CHIP_ESP32S3)
        model = "ESP32-S3";
    else if (chip_info.model == CHIP_ESP32S2)
        model = "ESP32-S2";
    else if (chip_info.model == CHIP_ESP32C3)
        model = "ESP32-C3";
    else if (chip_info.model == CHIP_ESP32C6)
        model = "ESP32-C6";

    ESP_LOGI(TAG, "--- SYSTEM INFO ---");
    ESP_LOGI(TAG, "Chip: %s rev v%d.%d, %d cores, %lu MHz",
             model, chip_info.revision / 100, (chip_info.revision % 100) / 10,
             chip_info.cores, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    ESP_LOGI(TAG, "Features: WiFi%s%s",
             chip_info.features & CHIP_FEATURE_BT ? " + BT" : "",
             chip_info.features & CHIP_FEATURE_BLE ? " + BLE" : "");

    /* PSRAM */
    size_t psram_size = esp_psram_get_size();
    if (psram_size > 0)
    {
        ESP_LOGI(TAG, "PSRAM: %d MB", (int)(psram_size / (1024 * 1024)));
    }
    else
    {
        ESP_LOGI(TAG, "PSRAM: none");
    }

    /* Flash */
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    ESP_LOGI(TAG, "Flash: %d MB", (int)(flash_size / (1024 * 1024)));

    /* Free heap */
    ESP_LOGI(TAG, "Free heap: internal %d KB, PSRAM %d KB",
             (int)(esp_get_free_heap_size() / 1024),
             (int)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

    /* ── Known‑panel database ──────────────────────────────────────────
     * Each entry maps a board's display pins, touch controller, and
     * resolution.  Community contributions welcome — add your board here. */
    ESP_LOGI(TAG, "--- PANEL MATCH ---");
    bool matched = false;

    /* Elecrow CrowPanel 2.8" (verified — our diagnostic hardware) */
    if (!matched &&
        LCD_SCLK_GPIO == 42 && LCD_MOSI_GPIO == 39 && LCD_DC_GPIO == 41 &&
        LCD_BL_GPIO == 38 && I2C_SDA_GPIO == 15 && I2C_SCL_GPIO == 16 &&
        LCD_H_RES == 320 && LCD_V_RES == 240 && touch_addr == 0x38)
    {
        ESP_LOGI(TAG, "✅ Matched: Elecrow CrowPanel 2.8\"");
        matched = true;
    }

    /* LilyGO T-Display-S3 Touch (verified: GitHub issue #318, ProtoSupplies) */
    if (!matched &&
        LCD_SCLK_GPIO == -1 && LCD_MOSI_GPIO == -1 && LCD_DC_GPIO == -1 &&
        I2C_SDA_GPIO == 18 && I2C_SCL_GPIO == 17 &&
        LCD_H_RES == 170 && LCD_V_RES == 320 &&
        (touch_addr == 0x5D || touch_addr == 0x14))
    {
        ESP_LOGI(TAG, "✅ Matched: LilyGO T-Display-S3 Touch (GT911)");
        matched = true;
    }

    /* M5Stack Core2 (verified: DeepWiki, Zephyr docs)
     * Display: ILI9342C via SPI — MOSI=23, SCLK=18, CS=14, DC=27, RST=33
     * Backlight: GPIO 32  |  Touch: FT6336U @ 0x38, SDA=21, SCL=22 */
    if (!matched &&
        LCD_SCLK_GPIO == 18 && LCD_MOSI_GPIO == 23 && LCD_DC_GPIO == 27 &&
        LCD_BL_GPIO == 32 && I2C_SDA_GPIO == 21 && I2C_SCL_GPIO == 22 &&
        LCD_H_RES == 320 && LCD_V_RES == 240 && touch_addr == 0x38)
    {
        ESP_LOGI(TAG, "✅ Matched: M5Stack Core2 (ILI9342C + FT6336U)");
        matched = true;
    }

    /* ESP32-S3-BOX-3 (verified: Espressif BSP component registry v3.0.0)
     * Display: ST7789 via SPI — MOSI=6, SCLK=7, CS=5, DC=4, RST=48
     * Backlight: GPIO 47  |  Touch: GT911/TT21100, SDA=41, SCL=40 */
    if (!matched &&
        LCD_SCLK_GPIO == 7 && LCD_MOSI_GPIO == 6 && LCD_DC_GPIO == 4 &&
        LCD_BL_GPIO == 47 && I2C_SDA_GPIO == 41 && I2C_SCL_GPIO == 40 &&
        LCD_H_RES == 320 && LCD_V_RES == 240 &&
        (touch_addr == 0x5D || touch_addr == 0x14))
    {
        ESP_LOGI(TAG, "✅ Matched: ESP32-S3-BOX-3 (ST7789 + GT911/TT21100)");
        matched = true;
    }

    /* Waveshare ESP32-S3 Touch LCD 2.8\" (verified: Bruce firmware issue #2394)
     * Display: ST7789 via SPI — MOSI=45, SCLK=40, CS=42, DC=41, RST=39, BL=5
     * Touch: CST328 @ I2C, SDA=1, SCL=3  */
    if (!matched &&
        LCD_SCLK_GPIO == 40 && LCD_MOSI_GPIO == 45 && LCD_DC_GPIO == 41 &&
        LCD_BL_GPIO == 5 && I2C_SDA_GPIO == 1 && I2C_SCL_GPIO == 3 &&
        LCD_H_RES == 320 && LCD_V_RES == 240)
    {
        ESP_LOGI(TAG, "✅ Matched: Waveshare ESP32-S3 Touch LCD 2.8\" (ST7789 + CST328)");
        matched = true;
    }

    if (!matched)
    {
        ESP_LOGI(TAG, "⚠️  No known panel matched. Consider adding your configuration.");
        ESP_LOGI(TAG, "   Open an issue or PR with your pin definitions at:");
        ESP_LOGI(TAG, "   https://github.com/SS-Sauron/Bluetooth-Jieli-Research");
    }

    /* ── Pin summary (unchanged) ───────────────────────────────────────── */
    ESP_LOGI(TAG, "--- PIN SUMMARY ---");
    ESP_LOGI(TAG, "Display SPI: SCLK=%d MOSI=%d DC=%d", LCD_SCLK_GPIO, LCD_MOSI_GPIO, LCD_DC_GPIO);
    ESP_LOGI(TAG, "Backlight:   GPIO %d", LCD_BL_GPIO);
    ESP_LOGI(TAG, "Touch I2C:   SDA=%d SCL=%d", I2C_SDA_GPIO, I2C_SCL_GPIO);
    ESP_LOGI(TAG, "--------------------------------");

    /* ── Strapping pin diagnostics ──────────────────────────────────── */
    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_0, GPIO_PULLUP_ONLY);
    int gpio0_level = gpio_get_level(GPIO_NUM_0);
    ESP_LOGI(TAG, "GPIO0 strapping pin: %s (%s)",
             gpio0_level ? "HIGH" : "LOW",
             gpio0_level ? "Normal boot" : "Download mode — disconnect any external pull‑down");

    /* ── Support references ──────────────────────────────────────────── */
    ESP_LOGI(TAG, "--- SUPPORT ---");
    ESP_LOGI(TAG, "For deep‑dive debugging, try the official Espressif AI chat:");
    ESP_LOGI(TAG, "https://chat.espressif.com/");
    ESP_LOGI(TAG, "Or open an issue with this diagnostic output at your board vendor's repository.");
    ESP_LOGI(TAG, "Common open‑source display driver repos: TFT_eSPI, lvgl, esp_lvgl_port.");
    ESP_LOGI(TAG, "--------------------------------");

    /* Done — just idle forever */
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
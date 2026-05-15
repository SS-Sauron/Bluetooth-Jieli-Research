#include "nrf24_radio.h"

#ifdef CONFIG_SCANNER_NRF24

#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define NRF24_CE_GPIO GPIO_NUM_4
#define NRF24_CSN_GPIO GPIO_NUM_5
#define NRF24_SCK_GPIO GPIO_NUM_18
#define NRF24_MOSI_GPIO GPIO_NUM_23
#define NRF24_MISO_GPIO GPIO_NUM_19

#define NRF24_SPI_HOST SPI2_HOST
#define NRF24_SPI_CLOCK_HZ (4 * 1000 * 1000)

#define NRF24_CMD_R_REGISTER 0x00
#define NRF24_CMD_W_REGISTER 0x20
#define NRF24_CMD_FLUSH_RX 0xE2
#define NRF24_CMD_NOP 0xFF

#define NRF24_REG_CONFIG 0x00
#define NRF24_REG_EN_AA 0x01
#define NRF24_REG_EN_RXADDR 0x02
#define NRF24_REG_SETUP_AW 0x03
#define NRF24_REG_SETUP_RETR 0x04
#define NRF24_REG_RF_CH 0x05
#define NRF24_REG_RF_SETUP 0x06
#define NRF24_REG_STATUS 0x07
#define NRF24_REG_RPD 0x09
#define NRF24_REG_RX_ADDR_P0 0x0A
#define NRF24_REG_RX_ADDR_P1 0x0B
#define NRF24_REG_RX_ADDR_P2 0x0C
#define NRF24_REG_RX_ADDR_P3 0x0D
#define NRF24_REG_RX_ADDR_P4 0x0E
#define NRF24_REG_RX_ADDR_P5 0x0F
#define NRF24_REG_DYNPD 0x1C

#define NRF24_CONFIG_PWR_UP 0x02
#define NRF24_CONFIG_PRIM_RX 0x01
#define NRF24_CONFIG_STANDBY NRF24_CONFIG_PWR_UP
#define NRF24_CONFIG_RX (NRF24_CONFIG_PWR_UP | NRF24_CONFIG_PRIM_RX)
#define NRF24_STATUS_RESET_VALUE 0x0E
#define NRF24_CHANNEL_COUNT 126
#define NRF24_SPECTRUM_COLUMNS (NRF24_CHANNEL_COUNT / 2)
#define NRF24_SPECTRUM_INTERVAL_MS 5000
#define NRF24_PROMISCUOUS_SETTLE_US 2000

static const char *TAG = "NRF24";
static spi_device_handle_t s_spi;
static bool s_present;
static bool s_bus_initialized;

static esp_err_t nrf24_read_reg(uint8_t reg, uint8_t *value)
{
    if (!s_spi || !value)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx[2] = {NRF24_CMD_R_REGISTER | (reg & 0x1F), NRF24_CMD_NOP};
    uint8_t rx[2] = {0};
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.length = sizeof(tx) * 8;
    trans.tx_buffer = tx;
    trans.rx_buffer = rx;

    esp_err_t ret = spi_device_transmit(s_spi, &trans);
    if (ret == ESP_OK)
    {
        *value = rx[1];
    }
    return ret;
}

static esp_err_t nrf24_write_reg(uint8_t reg, uint8_t value)
{
    if (!s_spi)
    {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t tx[2] = {NRF24_CMD_W_REGISTER | (reg & 0x1F), value};
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.length = sizeof(tx) * 8;
    trans.tx_buffer = tx;

    return spi_device_transmit(s_spi, &trans);
}

static esp_err_t nrf24_write_buf(uint8_t reg, const uint8_t *data, size_t len)
{
    if (!s_spi)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || len == 0 || len > 2)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx[3] = {NRF24_CMD_W_REGISTER | (reg & 0x1F), 0, 0};
    memcpy(&tx[1], data, len);

    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.length = (len + 1) * 8;
    trans.tx_buffer = tx;

    return spi_device_transmit(s_spi, &trans);
}

static esp_err_t nrf24_send_cmd(uint8_t cmd)
{
    if (!s_spi)
    {
        return ESP_ERR_INVALID_STATE;
    }

    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.length = 8;
    trans.tx_buffer = &cmd;

    return spi_device_transmit(s_spi, &trans);
}

static esp_err_t nrf24_flush_rx(void)
{
    return nrf24_send_cmd(NRF24_CMD_FLUSH_RX);
}

static void nrf24_standby(void)
{
    gpio_set_level(NRF24_CE_GPIO, 0);
}

static esp_err_t nrf24_promiscuous_setup(void)
{
    const uint8_t rx_addr_p0[2] = {0x55, 0x55};
    const uint8_t rx_addr_p1[2] = {0xAA, 0xAA};
    const uint8_t rx_addr_p2[2] = {0xA0, 0xAA};
    const uint8_t rx_addr_p3[2] = {0xAB, 0xAA};
    const uint8_t rx_addr_p4[2] = {0xAC, 0xAA};
    const uint8_t rx_addr_p5[2] = {0xAD, 0xAA};
    esp_err_t ret;

    nrf24_standby();

    ret = nrf24_write_reg(NRF24_REG_EN_AA, 0x00);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nrf24_write_reg(NRF24_REG_EN_RXADDR, 0x3F);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nrf24_write_reg(NRF24_REG_SETUP_AW, 0x01);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nrf24_write_reg(NRF24_REG_SETUP_RETR, 0x00);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nrf24_write_reg(NRF24_REG_DYNPD, 0x00);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nrf24_write_reg(NRF24_REG_RF_SETUP, 0x00);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nrf24_write_buf(NRF24_REG_RX_ADDR_P0, rx_addr_p0, sizeof(rx_addr_p0));
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nrf24_write_buf(NRF24_REG_RX_ADDR_P1, rx_addr_p1, sizeof(rx_addr_p1));
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nrf24_write_buf(NRF24_REG_RX_ADDR_P2, rx_addr_p2, sizeof(rx_addr_p2));
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nrf24_write_buf(NRF24_REG_RX_ADDR_P3, rx_addr_p3, sizeof(rx_addr_p3));
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nrf24_write_buf(NRF24_REG_RX_ADDR_P4, rx_addr_p4, sizeof(rx_addr_p4));
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nrf24_write_buf(NRF24_REG_RX_ADDR_P5, rx_addr_p5, sizeof(rx_addr_p5));
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nrf24_write_reg(NRF24_REG_CONFIG, NRF24_CONFIG_STANDBY);
    if (ret != ESP_OK)
    {
        return ret;
    }

    esp_rom_delay_us(NRF24_PROMISCUOUS_SETTLE_US);
    return ESP_OK;
}

static void nrf24_remove_device(void)
{
    if (s_spi)
    {
        spi_bus_remove_device(s_spi);
        s_spi = NULL;
    }

    if (s_bus_initialized)
    {
        spi_bus_free(NRF24_SPI_HOST);
        s_bus_initialized = false;
    }

    s_present = false;
}

bool nrf24_radio_init(void)
{
    if (s_present)
    {
        return true;
    }

    gpio_config_t ce_conf = {
        .pin_bit_mask = (1ULL << NRF24_CE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&ce_conf);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "CE GPIO config failed: %s", esp_err_to_name(ret));
        return false;
    }
    gpio_set_level(NRF24_CE_GPIO, 0);

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = NRF24_MOSI_GPIO,
        .miso_io_num = NRF24_MISO_GPIO,
        .sclk_io_num = NRF24_SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        /* Promiscuous setup writes 2-byte noise addresses plus the command byte. */
        .max_transfer_sz = 3,
    };

    ret = spi_bus_initialize(NRF24_SPI_HOST, &bus_cfg, SPI_DMA_DISABLED);
    if (ret == ESP_OK)
    {
        s_bus_initialized = true;
    }
    else if (ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return false;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = NRF24_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = NRF24_CSN_GPIO,
        .queue_size = 1,
    };

    ret = spi_bus_add_device(NRF24_SPI_HOST, &dev_cfg, &s_spi);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        if (s_bus_initialized)
        {
            spi_bus_free(NRF24_SPI_HOST);
            s_bus_initialized = false;
        }
        return false;
    }

    uint8_t status = 0;
    ret = nrf24_read_reg(NRF24_REG_STATUS, &status);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "STATUS read failed: %s", esp_err_to_name(ret));
        nrf24_remove_device();
        return false;
    }

    ESP_LOGI(TAG, "STATUS=0x%02x", status);

    if (status != NRF24_STATUS_RESET_VALUE)
    {
        nrf24_remove_device();
        return false;
    }

    s_present = true;
    ret = nrf24_promiscuous_setup();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Promiscuous setup failed: %s", esp_err_to_name(ret));
        nrf24_remove_device();
        return false;
    }

    return true;
}

void nrf24_spectrum_scan(uint8_t rssi[126])
{
    if (!s_present || !s_spi || !rssi)
    {
        return;
    }

    for (uint8_t channel = 0; channel < NRF24_CHANNEL_COUNT; channel++)
    {
        uint8_t rpd = 0;

        if (nrf24_write_reg(NRF24_REG_RF_CH, channel) != ESP_OK ||
            nrf24_write_reg(NRF24_REG_CONFIG, NRF24_CONFIG_STANDBY) != ESP_OK ||
            nrf24_flush_rx() != ESP_OK ||
            nrf24_write_reg(NRF24_REG_CONFIG, NRF24_CONFIG_RX) != ESP_OK)
        {
            rssi[channel] = 0;
            nrf24_standby();
            continue;
        }

        gpio_set_level(NRF24_CE_GPIO, 1);
        esp_rom_delay_us(150);

        if (nrf24_read_reg(NRF24_REG_RPD, &rpd) == ESP_OK)
        {
            rssi[channel] = (rpd & 0x01) ? 1 : 0;
        }
        else
        {
            rssi[channel] = 0;
        }

        nrf24_standby();
        (void)nrf24_write_reg(NRF24_REG_CONFIG, NRF24_CONFIG_STANDBY);
    }
}

void nrf24_spectrum_task(void *arg)
{
    (void)arg;

    if (!s_present || !s_spi)
    {
        vTaskDelete(NULL);
        return;
    }

    uint8_t rssi[NRF24_CHANNEL_COUNT];
    char spectrum[NRF24_SPECTRUM_COLUMNS + 1];

    while (1)
    {
        if (!s_present || !s_spi)
        {
            vTaskDelete(NULL);
            return;
        }

        memset(rssi, 0, sizeof(rssi));
        nrf24_spectrum_scan(rssi);

        for (uint8_t column = 0; column < NRF24_SPECTRUM_COLUMNS; column++)
        {
            uint8_t first = column * 2;
            spectrum[column] = (rssi[first] || rssi[first + 1]) ? '#' : '.';
        }
        spectrum[NRF24_SPECTRUM_COLUMNS] = '\0';

        printf("NRF24 spectrum 2400 MHz ... 2525 MHz\n");
        printf("%s\n", spectrum);

        vTaskDelay(pdMS_TO_TICKS(NRF24_SPECTRUM_INTERVAL_MS));
    }
}

void nrf24_jammer_start(void)
{
    ESP_LOGW(TAG, "NRF24 jammer start is not implemented");
}

void nrf24_jammer_stop(void)
{
    ESP_LOGW(TAG, "NRF24 jammer stop is not implemented");
}

#endif /* CONFIG_SCANNER_NRF24 */

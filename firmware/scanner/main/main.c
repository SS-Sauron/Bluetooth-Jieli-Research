/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

/****************************************************************************
 *
 * This file is for Classic Bluetooth device and service discovery Demo.
 *
 ****************************************************************************/

#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_gap_ble_api.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "espnow_proto.h"
#include "driver/gpio.h"

#define GAP_TAG "GAP"
#define MAX_TRACKED_DEVICES 64
#define MAX_DISPLAY_ROWS 12
#define SCANNER_DEBUG 1

#ifndef ESP_BLE_AD_TYPE_MANUFACTURER
#define ESP_BLE_AD_TYPE_MANUFACTURER 0xFF
#endif

#define A_RST "\033[0m"
#define A_CYAN "\033[36m"
#define A_GREEN "\033[32m"

#define SCAN_LED_GPIO GPIO_NUM_2
#define SCAN_LED_ON_LEVEL 0
#define SCAN_LED_OFF_LEVEL 1

static const char *TAG = "BT_SCANNER";
static volatile bool g_scan_enabled = true; // start scanning at boot
static volatile bool g_classic_discovering = false;
static volatile bool g_ble_scan_params_ready = false;
static volatile bool g_ble_scan_start_pending = false;
static volatile bool g_ble_scan_stop_pending = false;
static volatile bool g_ble_scanning = false;
static volatile bool g_show_table = true;
static uint32_t g_scan_cycle_count = 0;
static volatile bool g_ble_active_scan = true; // true = active (names/UUIDs), false = passive (stealth)
static volatile bool g_new_device_blink_pending = false;

/* Attack ESP32 peer MAC — shared by espnow_init() and send_device_over_espnow() */
static const uint8_t s_attack_mac[6] = PEER_ATTACK_MAC;

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50,
    .scan_window = 0x30,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
};

typedef enum
{
    APP_GAP_STATE_IDLE = 0,
    APP_GAP_STATE_DEVICE_DISCOVERING,
    APP_GAP_STATE_DEVICE_DISCOVER_COMPLETE,
    APP_GAP_STATE_SERVICE_DISCOVERING,
    APP_GAP_STATE_SERVICE_DISCOVER_COMPLETE,
} app_gap_state_t;

typedef struct
{
    uint8_t bdname_len;
    uint8_t eir_len;
    int8_t rssi;
    uint32_t cod;
    uint8_t eir[ESP_BT_GAP_EIR_DATA_LEN];
    uint8_t bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
    esp_bd_addr_t bda;
    app_gap_state_t state;
} app_gap_cb_t;

typedef struct
{
    esp_bd_addr_t bda;
    bool in_use;
    uint32_t last_seen_ms;
    int8_t rssi;
    uint32_t cod;
    uint8_t type; /* 0=Classic, 1=BLE */
    int8_t tx_power;
    uint16_t company_id;
    char vendor[32];
    uint8_t bdname_len;
    uint8_t bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
} device_entry_t;

static app_gap_cb_t m_dev_info;
static device_entry_t g_devices[MAX_TRACKED_DEVICES];

static char *bda2str(esp_bd_addr_t bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18)
    {
        return "";
    }

    uint8_t *p = bda;
    snprintf(str, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

static char *uuid2str(esp_bt_uuid_t *uuid, char *str, size_t size)
{
    if (uuid == NULL || str == NULL)
    {
        return "";
    }

    if (uuid->len == 2 && size >= 5)
    {
        snprintf(str, size, "%04x", uuid->uuid.uuid16);
    }
    else if (uuid->len == 4 && size >= 9)
    {
        snprintf(str, size, "%08" PRIx32, uuid->uuid.uuid32);
    }
    else if (uuid->len == 16 && size >= 37)
    {
        uint8_t *p = uuid->uuid.uuid128;
        snprintf(str, size, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 p[15], p[14], p[13], p[12], p[11], p[10], p[9], p[8],
                 p[7], p[6], p[5], p[4], p[3], p[2], p[1], p[0]);
    }
    else
    {
        return "";
    }

    return str;
}

static bool get_name_from_eir(uint8_t *eir, uint8_t *bdname, uint8_t *bdname_len)
{
    uint8_t *rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;

    if (!eir)
    {
        return false;
    }

    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname)
    {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (rmt_bdname)
    {
        if (rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN)
        {
            rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }

        if (bdname)
        {
            memcpy(bdname, rmt_bdname, rmt_bdname_len);
            bdname[rmt_bdname_len] = '\0';
        }
        if (bdname_len)
        {
            *bdname_len = rmt_bdname_len;
        }
        return true;
    }

    return false;
}

static inline uint32_t get_now_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static void device_entry_set_defaults(device_entry_t *entry)
{
    if (!entry)
    {
        return;
    }

    entry->tx_power = 127;
    entry->company_id = 0x0000;
    entry->type = 1;
    strncpy(entry->vendor, "(unknown)", sizeof(entry->vendor) - 1);
    entry->vendor[sizeof(entry->vendor) - 1] = '\0';
}

static void scan_led_set(bool on)
{
    gpio_set_level(SCAN_LED_GPIO, on ? SCAN_LED_ON_LEVEL : SCAN_LED_OFF_LEVEL);
}

static void scan_led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SCAN_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    scan_led_set(false);
}

static void scan_led_task(void *arg)
{
    (void)arg;

    while (1)
    {
        if (!g_scan_enabled)
        {
            g_new_device_blink_pending = false;
            scan_led_set(false);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (g_new_device_blink_pending)
        {
            g_new_device_blink_pending = false;
            for (int i = 0; i < 2 && g_scan_enabled; i++)
            {
                scan_led_set(true);
                vTaskDelay(pdMS_TO_TICKS(100));
                scan_led_set(false);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            continue;
        }

        scan_led_set(true);
        vTaskDelay(pdMS_TO_TICKS(100));
        scan_led_set(false);
        vTaskDelay(pdMS_TO_TICKS(900));
    }
}

static void update_entry_name(device_entry_t *entry, const uint8_t *bdname, uint8_t bdname_len)
{
    if (!entry || !bdname || bdname_len == 0)
    {
        return;
    }

    if (bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN)
    {
        bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
    }

    memcpy(entry->bdname, bdname, bdname_len);
    entry->bdname[bdname_len] = '\0';
    entry->bdname_len = bdname_len;
}

static uint8_t *find_valid_ble_ad(uint8_t *adv_data, uint8_t adv_len,
                                  uint8_t *scan_rsp, uint8_t scan_rsp_len,
                                  uint8_t ad_type, uint8_t min_len,
                                  uint8_t *out_len)
{
    uint8_t data_len = 0;
    uint8_t *data = esp_ble_resolve_adv_data_by_type(adv_data, adv_len, ad_type, &data_len);
    if (data && data_len >= min_len)
    {
        if (out_len)
        {
            *out_len = data_len;
        }
        return data;
    }

    data_len = 0;
    data = esp_ble_resolve_adv_data_by_type(scan_rsp, scan_rsp_len, ad_type, &data_len);
    if (data && data_len >= min_len)
    {
        if (out_len)
        {
            *out_len = data_len;
        }
        return data;
    }

    return NULL;
}

static void update_ble_metadata(device_entry_t *entry,
                                uint8_t *adv_data, uint8_t adv_len,
                                uint8_t *scan_rsp, uint8_t scan_rsp_len)
{
    uint8_t data_len = 0;
    uint8_t *data = find_valid_ble_ad(adv_data, adv_len, scan_rsp, scan_rsp_len,
                                      ESP_BLE_AD_TYPE_TX_PWR, 1, &data_len);
    if (data)
    {
        entry->tx_power = *((int8_t *)data);
    }

    data = find_valid_ble_ad(adv_data, adv_len, scan_rsp, scan_rsp_len,
                             ESP_BLE_AD_TYPE_MANUFACTURER, 2, &data_len);
    if (data)
    {
        entry->company_id = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    }
}

static const char *cod_major_label(uint32_t cod)
{
    if (!esp_bt_gap_is_valid_cod(cod))
    {
        return "Unknown";
    }

    switch (esp_bt_gap_get_cod_major_dev(cod))
    {
    case 0x0100:
        return "Computer";
    case 0x0200:
        return "Phone";
    case 0x0300:
        return "LAN";
    case 0x0400:
        return "Audio";
    case 0x0500:
        return "Peripheral";
    case 0x0600:
        return "Imaging";
    case 0x0700:
        return "Wearable";
    case 0x0800:
        return "Toy";
    case 0x0900:
        return "Health";
    default:
        return "Unknown";
    }
}

static device_entry_t *find_or_create_device(esp_bd_addr_t bda)
{
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++)
    {
        if (g_devices[i].in_use && memcmp(g_devices[i].bda, bda, ESP_BD_ADDR_LEN) == 0)
        {
            return &g_devices[i];
        }
    }

    for (int i = 0; i < MAX_TRACKED_DEVICES; i++)
    {
        if (!g_devices[i].in_use)
        {
            memset(&g_devices[i], 0, sizeof(g_devices[i]));
            device_entry_set_defaults(&g_devices[i]);
            memcpy(g_devices[i].bda, bda, ESP_BD_ADDR_LEN);
            g_devices[i].in_use = true;
            return &g_devices[i];
        }
    }

    return NULL;
}

static void classic_scan_start(void)
{
    if (!g_scan_enabled || g_classic_discovering)
    {
        return;
    }

    app_gap_cb_t *p_dev = &m_dev_info;
    p_dev->state = APP_GAP_STATE_DEVICE_DISCOVERING;

    esp_err_t ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 48, 0);
    if (ret == ESP_OK)
    {
        g_classic_discovering = true;
    }
    else
    {
        ESP_LOGW(GAP_TAG, "Classic discovery start failed: %s", esp_err_to_name(ret));
    }
}

static void classic_scan_stop(void)
{
    esp_err_t ret = esp_bt_gap_cancel_discovery();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(GAP_TAG, "Classic discovery cancel failed: %s", esp_err_to_name(ret));
    }
}

static void ble_scan_start(void)
{
    if (!g_scan_enabled || !g_ble_scan_params_ready || g_ble_scanning || g_ble_scan_start_pending)
    {
        return;
    }

    esp_err_t ret = esp_ble_gap_start_scanning(0);
    if (ret == ESP_OK)
    {
        g_ble_scan_start_pending = true;
    }
    else if (ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "BLE scan start failed: %s", esp_err_to_name(ret));
    }
}

static void ble_scan_stop(void)
{
    if (!g_ble_scan_params_ready || g_ble_scan_stop_pending)
    {
        return;
    }

    esp_err_t ret = esp_ble_gap_stop_scanning();
    if (ret == ESP_OK)
    {
        g_ble_scan_stop_pending = true;
    }
    else if (ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "BLE scan stop failed: %s", esp_err_to_name(ret));
    }
}

static void log_ble_uuid128(const uint8_t *uuid)
{
    char uuid_str[37];

    snprintf(uuid_str, sizeof(uuid_str),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             uuid[15], uuid[14], uuid[13], uuid[12], uuid[11], uuid[10], uuid[9], uuid[8],
             uuid[7], uuid[6], uuid[5], uuid[4], uuid[3], uuid[2], uuid[1], uuid[0]);
    ESP_LOGI(TAG, "--Service UUID: %s", uuid_str);
}

static void log_ble_service_uuids(uint8_t *adv_data, uint16_t adv_len)
{
    uint16_t offset = 0;

    while (offset < adv_len)
    {
        uint8_t len = adv_data[offset];
        if (len == 0)
        {
            break;
        }
        if ((uint16_t)(offset + len + 1) > adv_len)
        {
            break;
        }

        uint8_t type = adv_data[offset + 1];
        uint8_t data_len = len - 1;
        uint8_t *data = adv_data + offset + 2;

        switch (type)
        {
        case ESP_BLE_AD_TYPE_16SRV_PART:
        case ESP_BLE_AD_TYPE_16SRV_CMPL:
            for (uint8_t i = 0; i + 1 < data_len; i += 2)
            {
                uint16_t uuid = data[i] | (data[i + 1] << 8);
                ESP_LOGI(TAG, "--Service UUID: %04x", uuid);
            }
            break;
        case ESP_BLE_AD_TYPE_32SRV_PART:
        case ESP_BLE_AD_TYPE_32SRV_CMPL:
            for (uint8_t i = 0; i + 3 < data_len; i += 4)
            {
                uint32_t uuid = (uint32_t)data[i] | ((uint32_t)data[i + 1] << 8) |
                                ((uint32_t)data[i + 2] << 16) | ((uint32_t)data[i + 3] << 24);
                ESP_LOGI(TAG, "--Service UUID: %08" PRIx32, uuid);
            }
            break;
        case ESP_BLE_AD_TYPE_128SRV_PART:
        case ESP_BLE_AD_TYPE_128SRV_CMPL:
            for (uint8_t i = 0; i + 15 < data_len; i += 16)
            {
                log_ble_uuid128(data + i);
            }
            break;
        default:
            break;
        }

        offset += len + 1;
    }
}

/* ── ESP-NOW helpers ────────────────────────────────────────────────────── */

/*
 * send_device_over_espnow() — pack a device_entry_t into a command_t and
 * fire it over ESP-NOW to the attack ESP32.  Fire-and-forget: errors are
 * logged but do not affect the scan loop.
 */
static void send_device_over_espnow(device_entry_t *entry)
{
    command_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    cmd.cmd_id = CMD_SEND_DEVICE;

    /* BD_ADDR */
    memcpy(cmd.payload.device.bda, entry->bda, sizeof(cmd.payload.device.bda));

    /* Name — device_info_t.name is char[32]; bdname is uint8_t[] */
    size_t name_len = entry->bdname_len < (sizeof(cmd.payload.device.name) - 1)
                          ? entry->bdname_len
                          : sizeof(cmd.payload.device.name) - 1;
    memcpy(cmd.payload.device.name, entry->bdname, name_len);
    cmd.payload.device.name[name_len] = '\0';

    cmd.payload.device.rssi = entry->rssi;
    cmd.payload.device.cod = entry->cod;
    cmd.payload.device.type = entry->type;

    esp_err_t ret = esp_now_send(s_attack_mac, (const uint8_t *)&cmd, sizeof(cmd));
    if (ret == ESP_OK)
    {
#if SCANNER_DEBUG
        ESP_LOGI(TAG, "ESP-NOW sent device %02x:%02x:%02x:%02x:%02x:%02x",
                 entry->bda[0], entry->bda[1], entry->bda[2],
                 entry->bda[3], entry->bda[4], entry->bda[5]);
#endif
    }
    else
    {
        ESP_LOGW(TAG, "ESP-NOW send failed: %s", esp_err_to_name(ret));
    }
}

/* ── BLE scan result handler ─────────────────────────────────────────────── */

static void log_ble_scan_result(esp_ble_gap_cb_param_t *param)
{
    char bda_str[18];
    uint8_t name_len = 0;
    uint8_t *name_ptr = NULL;
    device_entry_t *entry = find_or_create_device(param->scan_rst.bda);

    if (!entry)
    {
        ESP_LOGW(TAG, "BLE device table full, dropping %s",
                 bda2str(param->scan_rst.bda, bda_str, sizeof(bda_str)));
        return;
    }

    bool is_new = entry->last_seen_ms == 0;

    /* ── Resolve device name ─────────────────────────────────────────────
     * The advertising data and scan response are stored in one contiguous
     * buffer (ble_adv).  The scan response starts at offset adv_data_len.
     * Search the advertising portion first, then the scan response.
     * ─────────────────────────────────────────────────────────────────── */
    name_ptr = esp_ble_resolve_adv_data_by_type(
        param->scan_rst.ble_adv,
        param->scan_rst.adv_data_len,
        ESP_BLE_AD_TYPE_NAME_CMPL, &name_len);
    if (!name_ptr)
    {
        name_ptr = esp_ble_resolve_adv_data_by_type(
            param->scan_rst.ble_adv,
            param->scan_rst.adv_data_len,
            ESP_BLE_AD_TYPE_NAME_SHORT, &name_len);
    }
    if (!name_ptr)
    {
        uint8_t *scan_rsp = param->scan_rst.ble_adv + param->scan_rst.adv_data_len;
        name_ptr = esp_ble_resolve_adv_data_by_type(
            scan_rsp,
            param->scan_rst.scan_rsp_len,
            ESP_BLE_AD_TYPE_NAME_CMPL, &name_len);
    }
    if (!name_ptr)
    {
        uint8_t *scan_rsp = param->scan_rst.ble_adv + param->scan_rst.adv_data_len;
        name_ptr = esp_ble_resolve_adv_data_by_type(
            scan_rsp,
            param->scan_rst.scan_rsp_len,
            ESP_BLE_AD_TYPE_NAME_SHORT, &name_len);
    }

    if (name_ptr && name_len > 0)
    {
        update_entry_name(entry, name_ptr, name_len);
    }

    uint8_t *scan_rsp = param->scan_rst.ble_adv + param->scan_rst.adv_data_len;
    update_ble_metadata(entry,
                        param->scan_rst.ble_adv,
                        param->scan_rst.adv_data_len,
                        scan_rsp,
                        param->scan_rst.scan_rsp_len);

    entry->last_seen_ms = get_now_ms();
    entry->rssi = (int8_t)param->scan_rst.rssi;
    entry->type = 1;
    if (is_new)
    {
        entry->cod = 0;
    }

    if (!g_show_table)
    {
        ESP_LOGI(TAG, "%s: BLE %s RSSI=%d name=%s",
                 is_new ? "NEW" : "UPD",
                 bda2str(param->scan_rst.bda, bda_str, sizeof(bda_str)),
                 (int)entry->rssi,
                 entry->bdname_len > 0 ? (char *)entry->bdname : "(unknown)");

        /* ── Log service UUIDs ──────────────────────────────────────────
         * Search both parts of the contiguous buffer.
         * ─────────────────────────────────────────────────────────────── */
        if (is_new)
        {
            log_ble_service_uuids(param->scan_rst.ble_adv,
                                  param->scan_rst.adv_data_len);
            log_ble_service_uuids(param->scan_rst.ble_adv + param->scan_rst.adv_data_len,
                                  param->scan_rst.scan_rsp_len);
        }
    }

    if (is_new)
    {
        g_new_device_blink_pending = true;
    }

    /* Forward device to attack ESP32 over ESP-NOW */
    send_device_over_espnow(entry);
}

static void ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS)
        {
            g_ble_scan_params_ready = true;
            ble_scan_start();
        }
        else
        {
            ESP_LOGE(TAG, "BLE scan params failed: %d", param->scan_param_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        g_ble_scan_start_pending = false;
        if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
        {
            g_ble_scanning = true;
            ESP_LOGI(TAG, "BLE scan started.");
            if (!g_scan_enabled)
            {
                ble_scan_stop();
            }
        }
        else
        {
            g_ble_scanning = false;
            ESP_LOGE(TAG, "BLE scan start failed: %d", param->scan_start_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        g_ble_scan_stop_pending = false;
        g_ble_scanning = false;
        ESP_LOGI(TAG, "BLE scan stopped.");
        if (g_scan_enabled)
        {
            ble_scan_start();
        }
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT)
        {
            log_ble_scan_result(param);
        }
        else if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT)
        {
            g_ble_scanning = false;
            if (g_scan_enabled)
            {
                ble_scan_start();
            }
        }
        break;
    default:
        ESP_LOGD(TAG, "BLE GAP event: %d", event);
        break;
    }
}

static void update_device_info(esp_bt_gap_cb_param_t *param)
{
    char bda_str[18];
    uint32_t cod = 0;
    int8_t rssi = -128; /* invalid value */
    uint8_t *bdname = NULL;
    uint8_t bdname_len = 0;
    uint8_t *eir = NULL;
    uint8_t eir_len = 0;
    esp_bt_gap_dev_prop_t *p;

    for (int i = 0; i < param->disc_res.num_prop; i++)
    {
        p = param->disc_res.prop + i;
        switch (p->type)
        {
        case ESP_BT_GAP_DEV_PROP_COD:
            cod = *(uint32_t *)(p->val);
            break;
        case ESP_BT_GAP_DEV_PROP_RSSI:
            rssi = *(int8_t *)(p->val);
            break;
        case ESP_BT_GAP_DEV_PROP_BDNAME:
            bdname_len = (p->len > ESP_BT_GAP_MAX_BDNAME_LEN) ? ESP_BT_GAP_MAX_BDNAME_LEN : (uint8_t)p->len;
            bdname = (uint8_t *)(p->val);
            break;
        case ESP_BT_GAP_DEV_PROP_EIR:
        {
            eir_len = p->len;
            eir = (uint8_t *)(p->val);
            break;
        }
        default:
            break;
        }
    }

    app_gap_cb_t *p_dev = &m_dev_info;

    memcpy(p_dev->bda, param->disc_res.bda, ESP_BD_ADDR_LEN);
    p_dev->cod = cod;
    p_dev->rssi = rssi;
    p_dev->bdname_len = 0;
    p_dev->eir_len = 0;
    memset(p_dev->bdname, 0, sizeof(p_dev->bdname));
    memset(p_dev->eir, 0, sizeof(p_dev->eir));

    if (bdname_len > 0)
    {
        memcpy(p_dev->bdname, bdname, bdname_len);
        p_dev->bdname[bdname_len] = '\0';
        p_dev->bdname_len = bdname_len;
    }
    if (eir_len > 0)
    {
        memcpy(p_dev->eir, eir, eir_len);
        p_dev->eir_len = eir_len;
    }

    if (p_dev->bdname_len == 0)
    {
        get_name_from_eir(p_dev->eir, p_dev->bdname, &p_dev->bdname_len);
    }

    device_entry_t *entry = find_or_create_device(param->disc_res.bda);
    if (!entry)
    {
        ESP_LOGW(GAP_TAG, "Classic device table full, dropping %s",
                 bda2str(param->disc_res.bda, bda_str, sizeof(bda_str)));
        p_dev->state = APP_GAP_STATE_DEVICE_DISCOVER_COMPLETE;
        return;
    }

    bool is_new = entry->last_seen_ms == 0;
    entry->last_seen_ms = get_now_ms();
    entry->rssi = rssi;
    entry->cod = cod;
    entry->type = 0;
    if (p_dev->bdname_len > 0)
    {
        update_entry_name(entry, p_dev->bdname, p_dev->bdname_len);
    }

    if (!g_show_table)
    {
        ESP_LOGI(GAP_TAG, "%s: Classic %s RSSI=%d COD=0x%" PRIx32 " name=%s",
                 is_new ? "NEW" : "UPD",
                 bda2str(param->disc_res.bda, bda_str, sizeof(bda_str)),
                 (int)entry->rssi,
                 entry->cod,
                 entry->bdname_len > 0 ? (char *)entry->bdname : "(unknown)");
    }

    if (is_new)
    {
        g_new_device_blink_pending = true;
    }

    /* Forward device to attack ESP32 over ESP-NOW */
    send_device_over_espnow(entry);

    p_dev->state = APP_GAP_STATE_DEVICE_DISCOVER_COMPLETE;
}

static void bt_app_gap_init(void)
{
    app_gap_cb_t *p_dev = &m_dev_info;
    memset(p_dev, 0, sizeof(app_gap_cb_t));

    p_dev->state = APP_GAP_STATE_IDLE;
}

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    app_gap_cb_t *p_dev = &m_dev_info;
    char bda_str[18];
    char uuid_str[37];

    switch (event)
    {
    case ESP_BT_GAP_DISC_RES_EVT:
    {
        update_device_info(param);
        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
    {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED)
        {
            g_classic_discovering = false;
            ESP_LOGI(GAP_TAG, "Device discovery stopped.");
            if (g_scan_enabled)
            {
                classic_scan_start();
            }
            else
            {
                ESP_LOGI(GAP_TAG, "Scanner paused. Type 'start' to resume.");
            }
        }
        else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED)
        {
            g_scan_cycle_count++;
            g_classic_discovering = true;
            ESP_LOGI(GAP_TAG, "Discovery started.");
            if (!g_scan_enabled)
            {
                classic_scan_stop();
            }
        }
        break;
    }
    case ESP_BT_GAP_RMT_SRVCS_EVT:
    {
        if (memcmp(param->rmt_srvcs.bda, p_dev->bda, ESP_BD_ADDR_LEN) == 0 &&
            p_dev->state == APP_GAP_STATE_SERVICE_DISCOVERING)
        {
            p_dev->state = APP_GAP_STATE_SERVICE_DISCOVER_COMPLETE;
            if (param->rmt_srvcs.stat == ESP_BT_STATUS_SUCCESS)
            {
                ESP_LOGI(GAP_TAG, "Services for device %s found", bda2str(p_dev->bda, bda_str, sizeof(bda_str)));
                for (int i = 0; i < param->rmt_srvcs.num_uuids; i++)
                {
                    esp_bt_uuid_t *u = param->rmt_srvcs.uuid_list + i;
                    ESP_LOGI(GAP_TAG, "--%s", uuid2str(u, uuid_str, 37));
                }
            }
            else
            {
                ESP_LOGI(GAP_TAG, "Services for device %s not found", bda2str(p_dev->bda, bda_str, sizeof(bda_str)));
            }
        }
        break;
    }
    case ESP_BT_GAP_RMT_SRVC_REC_EVT:
        break;
    default:
    {
        ESP_LOGI(GAP_TAG, "event: %d", event);
        break;
    }
    }
    return;
}

static void bt_app_gap_start_up(void)
{
    /* register GAP callback function */
    esp_bt_gap_register_callback(bt_app_gap_cb);

    char *dev_name = "ESP_GAP_INQUIRY";
    esp_bt_gap_set_device_name(dev_name);

    /* set discoverable and connectable mode, wait to be connected */
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    /* initialize device information and status */
    bt_app_gap_init();

    /* start to discover nearby Bluetooth devices */
    classic_scan_start();
}

static void ble_set_scan_type(bool active)
{
    g_ble_active_scan = active;
    g_ble_scan_params_ready = false;

    // Stop any running scan
    if (g_ble_scanning)
    {
        esp_ble_gap_stop_scanning();
        // Wait for stop to complete — a small delay is sufficient
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // Update the parameter
    ble_scan_params.scan_type = active ? BLE_SCAN_TYPE_ACTIVE : BLE_SCAN_TYPE_PASSIVE;

    // Re‑register and start
    esp_err_t ret = esp_ble_gap_set_scan_params(&ble_scan_params);
    if (ret == ESP_OK)
    {
        g_ble_scan_params_ready = true;
        if (g_scan_enabled)
        {
            ble_scan_start();
        }
    }
}

static void ble_app_gap_start_up(void)
{
    esp_err_t ret = esp_ble_gap_register_callback(ble_gap_cb);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "BLE GAP callback register failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gap_set_scan_params(&ble_scan_params);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "BLE scan params set failed: %s", esp_err_to_name(ret));
    }
}

static void dashboard_task(void *arg)
{
    (void)arg;
    char bda_str[18];

    /* Column widths — single source of truth for header and device rows.
     * Change a value here and both the header label and the data column
     * automatically resize together.                                    */
    const int COL_MAC = 17;
    const int COL_TYPE = 7;
    const int COL_CLASS = 10;
    const int COL_RSSI = 5;
    const int COL_AGE = 13;

    while (1)
    {
        if (g_show_table)
        {
            uint32_t now_ms = get_now_ms();
            uint32_t displayed_rows = 0;
            uint32_t hidden_rows = 0;

            /* ── Build header — box_width is derived from its length ── */
            char header[128];
            snprintf(header, sizeof(header),
                     "Scan #%-5" PRIu32 "  %-*s  %-*s  %-*s  %*s  %*s  %s",
                     g_scan_cycle_count,
                     COL_MAC, "MAC",
                     COL_TYPE, "Type",
                     COL_CLASS, "Class",
                     COL_RSSI, "RSSI",
                     COL_AGE, "Last Seen (s)",
                     "Name");

            int content_width = (int)strlen(header);
            /* box_width = content + 1-space pad on each side */
            int box_width = content_width + 2;

            /* ── Clear screen and scrollback ── */
            printf("\033[2J\033[3J\033[H");

            /* ── Top border ── */
            printf("┌");
            for (int i = 0; i < box_width; i++)
                printf("─");
            printf("┐\n");

            /* ── Active/passive mode indicator ── */
            printf("│ %-*s │\n", content_width,
                   g_ble_active_scan ? "Mode: ACTIVE (names/UUIDs)" : "Mode: PASSIVE (stealth)");

            /* ── Header row ── */
            printf("│ %s │\n", header);

            /* ── Header / data divider ── */
            printf("├");
            for (int i = 0; i < box_width; i++)
                printf("─");
            printf("┤\n");

            /* ── Device rows ── */
            for (int i = 0; i < MAX_TRACKED_DEVICES; i++)
            {
                device_entry_t *entry = &g_devices[i];
                if (!entry->in_use)
                    continue;

                uint32_t age_ms = now_ms - entry->last_seen_ms;
                if (age_ms >= 120000)
                    continue;

                if (displayed_rows >= MAX_DISPLAY_ROWS)
                {
                    hidden_rows++;
                    continue;
                }

                bool is_classic = entry->type == 0;
                const char *type = is_classic ? "Classic" : "BLE";
                const char *row_color = is_classic ? A_GREEN : A_CYAN;
                const char *class_label = is_classic ? cod_major_label(entry->cod) : "BLE";
                const char *name = entry->bdname_len > 0
                                       ? (char *)entry->bdname
                                       : "(unknown)";

                /* Format age as "Xs" string so %*s can right-align it */
                char age_str[16];
                snprintf(age_str, sizeof(age_str), "%" PRIu32 "s", age_ms / 1000);

                /* Build row from the same COL_* widths as the header */
                char row[320]; // safe upper bound for formatted row + name
                snprintf(row, sizeof(row),
                         "%-*s  %-*s  %-*s  %*d  %*s  %s",
                         COL_MAC, bda2str(entry->bda, bda_str, sizeof(bda_str)),
                         COL_TYPE, type,
                         COL_CLASS, class_label,
                         COL_RSSI, (int)entry->rssi,
                         COL_AGE, age_str,
                         name);

                /* Truncate at content_width so a long name cannot break
                 * the right border.                                     */
                if ((int)strlen(row) > content_width)
                    row[content_width] = '\0';

                printf("%s│ %-*s │" A_RST "\n", row_color, content_width, row);
                displayed_rows++;
            }

            /* ── Empty-state row ── */
            if (displayed_rows == 0)
            {
                printf("│ %-*s │\n", content_width, "(no devices in range)");
            }

            /* ── Hidden-rows notice (sits between data and footer) ── */
            if (hidden_rows > 0)
            {
                char hidden_msg[128];
                snprintf(hidden_msg, sizeof(hidden_msg),
                         "... and %" PRIu32 " more device(s) - type 'table off' for full list.",
                         hidden_rows);
                printf("│ %-*s │\n", content_width, hidden_msg);
            }

            /* ── Footer divider ── */
            printf("├");
            for (int i = 0; i < box_width; i++)
                printf("─");
            printf("┤\n");

            /* ── Footer ── */
            printf("│ %-*s │\n", content_width,
                   "[table on|off] [start|stop] [active|passive]");

            /* ── Bottom border ── */
            printf("└");
            for (int i = 0; i < box_width; i++)
                printf("─");
            printf("┘\n");
        }

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

static void scan_control_task(void *arg)
{
    char buf[32];
    while (1)
    {
        if (fgets(buf, sizeof(buf), stdin))
        {
            if (strncmp(buf, "stop", 4) == 0)
            {
                g_scan_enabled = false;
                classic_scan_stop();
                ble_scan_stop();
                printf("Scan stopped.\n");
            }
            else if (strncmp(buf, "table on", 8) == 0)
            {
                g_show_table = true;
            }
            else if (strncmp(buf, "table off", 9) == 0)
            {
                g_show_table = false;
                printf("\033[2J\033[H");
                printf("Raw log view enabled.\n");
            }
            else if (strncmp(buf, "start", 5) == 0)
            {
                g_scan_enabled = true;
                classic_scan_start();
                ble_scan_start();
                printf("Scan started.\n");
            }
            else if (strncmp(buf, "active", 6) == 0)
            {
                ble_set_scan_type(true);
                printf("BLE scan: ACTIVE  (requests names & UUIDs — visible)\n");
            }
            else if (strncmp(buf, "passive", 7) == 0)
            {
                ble_set_scan_type(false);
                printf("BLE scan: PASSIVE (listen‑only — stealth)\n");
            }
            else if (buf[0] != '\n' && buf[0] != '\r' && buf[0] != '\0')
            {
                bool was_showing = g_show_table;
                g_show_table = false;

                printf("\nUnknown command: %s\n", buf);
                printf("Available commands:\n");
                printf("  start           - start scanning\n");
                printf("  stop            - stop scanning\n");
                printf("  table on        - show table view (paginated)\n");
                printf("  table off       - show raw log view (no pagination)\n");
                printf("  active          - active BLE scan (requests names/UUIDs)\n");
                printf("  passive         - passive BLE scan (listen-only)\n");
                printf("\nPress Enter to return to dashboard...\n");

                /* Consume a whole line — safe for arrow keys and other
                 * multi‑byte sequences that would otherwise corrupt the
                 * next fgets() read. */
                char dummy[8];
                fgets(dummy, sizeof(dummy), stdin);

                if (was_showing)
                {
                    g_show_table = true;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ── ESP-NOW initialisation ─────────────────────────────────────────────── */

/*
 * espnow_send_cb() — fire-and-forget send callback.
 * Errors are visible in the log from send_device_over_espnow(); no
 * additional action is required here.
 */
static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
    if (status != ESP_NOW_SEND_SUCCESS)
    {
        ESP_LOGW(TAG, "ESP-NOW send callback: delivery failed");
    }
}

/*
 * espnow_init() — bring up Wi-Fi STA + ESP-NOW and register the attack
 * ESP32 as the sole peer.  Called once from app_main() after Bluetooth
 * init completes.  The ESP32 supports BT+Wi-Fi coexistence natively.
 *
 * NVS is already initialised by app_main() before this call, so we do
 * not call nvs_flash_init() again here.
 */
static void espnow_init(void)
{
    /* netif + default event loop are required before esp_wifi_init() */
    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        /* ESP_ERR_INVALID_STATE means the loop already exists — that is fine */
        ESP_ERROR_CHECK(ret);
    }

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (!sta_netif)
    {
        ESP_LOGE(TAG, "espnow_init: failed to create default Wi-Fi STA netif");
        return;
    }

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

    /* Register attack ESP32 (78:1c:3c:a5:a8:d2) as peer */
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memcpy(peer.peer_addr, s_attack_mac, sizeof(peer.peer_addr));
    peer.channel = 0;
    peer.encrypt = false;

    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    ESP_LOGI(TAG, "ESP-NOW ready. Peer (attack ESP32): "
                  "%02x:%02x:%02x:%02x:%02x:%02x",
             s_attack_mac[0], s_attack_mac[1], s_attack_mac[2],
             s_attack_mac[3], s_attack_mac[4], s_attack_mac[5]);
}

void app_main(void)
{
    char bda_str[18] = {0};
    /* Initialize NVS — it is used to store PHY calibration data and save key-value pairs in flash memory*/
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    scan_led_init();

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK)
    {
        ESP_LOGE(GAP_TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM)) != ESP_OK)
    {
        ESP_LOGE(GAP_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg)) != ESP_OK)
    {
        ESP_LOGE(GAP_TAG, "%s initialize bluedroid failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bluedroid_enable()) != ESP_OK)
    {
        ESP_LOGE(GAP_TAG, "%s enable bluedroid failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(GAP_TAG, "Own address:[%s]", bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str, sizeof(bda_str)));
    bt_app_gap_start_up();
    ble_app_gap_start_up();

    /* Install UART driver and switch stdin to blocking mode so that
     * fgets() in scan_control_task blocks on each read rather than
     * spinning and returning EOF immediately.                        */
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0));
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

    /* Bring up ESP-NOW (Wi-Fi STA + peer registration) */
    espnow_init();

    xTaskCreate(scan_led_task, "scan_led", 1024, NULL, 1, NULL);
    xTaskCreate(scan_control_task, "scan_ctrl", 2048, NULL, 1, NULL);
    xTaskCreate(dashboard_task, "dashboard", 2048, NULL, 1, NULL);
}

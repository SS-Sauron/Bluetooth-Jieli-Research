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
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
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
#define ESPNOW_QUEUE_DEPTH 32
#define ESPNOW_SEND_MAX_ATTEMPTS 3
#define ESPNOW_RETRY_DELAY_MS 50
#define ESPNOW_CHANNEL 1
#define SCANNER_DEBUG 1

#ifndef ESP_BLE_AD_TYPE_MANUFACTURER
#define ESP_BLE_AD_TYPE_MANUFACTURER 0xFF
#endif

#define A_RST "\033[0m"
#define A_CYAN "\033[36m"
#define A_GREEN "\033[32m"

#define SCAN_LED_GPIO GPIO_NUM_2
#define SCAN_LED_ON_LEVEL 1  // active‑high: write 1 to turn LED ON
#define SCAN_LED_OFF_LEVEL 0 // active‑high: write 0 to turn LED OFF

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
static SemaphoreHandle_t g_scanner_mutex = NULL;
static QueueHandle_t g_espnow_queue = NULL;

/* Attack ESP32 peer MAC - shared by espnow_init() and send_device_over_espnow() */
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

typedef struct
{
    bool valid;
    esp_bd_addr_t bda;
    uint32_t last_seen_ms;
} device_eviction_log_t;

static app_gap_cb_t m_dev_info;
static device_entry_t g_devices[MAX_TRACKED_DEVICES];

static void scanner_mutex_lock(void)
{
    if (g_scanner_mutex)
    {
        xSemaphoreTake(g_scanner_mutex, portMAX_DELAY);
    }
}

static void scanner_mutex_unlock(void)
{
    if (g_scanner_mutex)
    {
        xSemaphoreGive(g_scanner_mutex);
    }
}

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

static void sanitize_name(uint8_t *name, size_t len)
{
    if (!name)
    {
        return;
    }

    for (size_t i = 0; i < len; i++)
    {
        if (name[i] < 0x20 || name[i] > 0x7e)
        {
            name[i] = '.';
        }
    }
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
        /* Shared scanner flags are read and updated while g_scanner_mutex is held. */
        scanner_mutex_lock();
        bool scan_enabled = g_scan_enabled;
        bool blink_pending = g_new_device_blink_pending;
        if (!scan_enabled || blink_pending)
        {
            g_new_device_blink_pending = false;
        }
        scanner_mutex_unlock();

        if (!scan_enabled)
        {
            scan_led_set(false); /* turn LED off (active‑high: level 0) */
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (blink_pending)
        {
            for (int i = 0; i < 2; i++)
            {
                scanner_mutex_lock();
                scan_enabled = g_scan_enabled;
                scanner_mutex_unlock();
                if (!scan_enabled)
                {
                    break;
                }
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
    sanitize_name(entry->bdname, bdname_len);
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

static void log_device_eviction(const device_eviction_log_t *eviction_log)
{
    if (!eviction_log || !eviction_log->valid)
    {
        return;
    }

    char evicted_bda[18];
    esp_bd_addr_t evicted_bda_addr;
    memcpy(evicted_bda_addr, eviction_log->bda, ESP_BD_ADDR_LEN);
    ESP_LOGD(TAG, "Evicting stale device entry %s last_seen=%" PRIu32 " ms",
             bda2str(evicted_bda_addr, evicted_bda, sizeof(evicted_bda)),
             eviction_log->last_seen_ms);
}

static device_entry_t *find_or_create_device(esp_bd_addr_t bda, device_eviction_log_t *eviction_log)
{
    /* Caller must hold g_scanner_mutex while accessing g_devices. */
    if (eviction_log)
    {
        memset(eviction_log, 0, sizeof(*eviction_log));
    }

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

    int oldest_index = 0;
    uint32_t oldest_seen_ms = g_devices[0].last_seen_ms;
    for (int i = 1; i < MAX_TRACKED_DEVICES; i++)
    {
        if (g_devices[i].last_seen_ms < oldest_seen_ms)
        {
            oldest_seen_ms = g_devices[i].last_seen_ms;
            oldest_index = i;
        }
    }

    if (eviction_log)
    {
        eviction_log->valid = true;
        memcpy(eviction_log->bda, g_devices[oldest_index].bda, ESP_BD_ADDR_LEN);
        eviction_log->last_seen_ms = oldest_seen_ms;
    }

    memset(&g_devices[oldest_index], 0, sizeof(g_devices[oldest_index]));
    device_entry_set_defaults(&g_devices[oldest_index]);
    memcpy(g_devices[oldest_index].bda, bda, ESP_BD_ADDR_LEN);
    g_devices[oldest_index].in_use = true;
    return &g_devices[oldest_index];
}

static esp_err_t set_scanner_discoverability(bool enable)
{
    esp_err_t ret = esp_bt_gap_set_scan_mode(
        enable ? ESP_BT_CONNECTABLE : ESP_BT_NON_CONNECTABLE,
        enable ? ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE);
    if (ret != ESP_OK)
    {
        ESP_LOGW(GAP_TAG, "Set scanner discoverability %s failed: %s",
                 enable ? "enabled" : "disabled",
                 esp_err_to_name(ret));
    }
    return ret;
}

static void classic_scan_start(void)
{
    scanner_mutex_lock();
    bool can_start = g_scan_enabled && !g_classic_discovering;
    if (can_start)
    {
        m_dev_info.state = APP_GAP_STATE_DEVICE_DISCOVERING;
    }
    scanner_mutex_unlock();

    if (!can_start)
    {
        return;
    }

    esp_err_t ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 48, 0);
    if (ret == ESP_OK)
    {
        scanner_mutex_lock();
        g_classic_discovering = true;
        scanner_mutex_unlock();
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
    scanner_mutex_lock();
    bool can_start = g_scan_enabled &&
                     g_ble_scan_params_ready &&
                     !g_ble_scanning &&
                     !g_ble_scan_start_pending;
    scanner_mutex_unlock();

    if (!can_start)
    {
        return;
    }

    esp_err_t ret = esp_ble_gap_start_scanning(0);
    if (ret == ESP_OK)
    {
        scanner_mutex_lock();
        g_ble_scan_start_pending = true;
        scanner_mutex_unlock();
    }
    else if (ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "BLE scan start failed: %s", esp_err_to_name(ret));
    }
}

static void ble_scan_stop(void)
{
    scanner_mutex_lock();
    bool can_stop = g_ble_scan_params_ready && !g_ble_scan_stop_pending;
    scanner_mutex_unlock();

    if (!can_stop)
    {
        return;
    }

    esp_err_t ret = esp_ble_gap_stop_scanning();
    if (ret == ESP_OK)
    {
        scanner_mutex_lock();
        g_ble_scan_stop_pending = true;
        scanner_mutex_unlock();
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
 * build_device_command_locked() - pack a device_entry_t into a command_t.
 * Caller must hold g_scanner_mutex while reading the device entry.
 */
static void build_device_command_locked(const device_entry_t *entry, command_t *cmd)
{
    memset(cmd, 0, sizeof(*cmd));

    cmd->version = ESPNOW_PROTO_VERSION;
    cmd->cmd_id = CMD_SEND_DEVICE;

    /* BD_ADDR */
    memcpy(cmd->payload.device.bda, entry->bda, sizeof(cmd->payload.device.bda));

    /* Name - device_info_t.name is char[32]; bdname is uint8_t[] */
    size_t name_len = entry->bdname_len < (sizeof(cmd->payload.device.name) - 1)
                          ? entry->bdname_len
                          : sizeof(cmd->payload.device.name) - 1;
    memcpy(cmd->payload.device.name, entry->bdname, name_len);
    cmd->payload.device.name[name_len] = '\0';

    cmd->payload.device.rssi = entry->rssi;
    cmd->payload.device.cod = entry->cod;
    cmd->payload.device.type = entry->type;
    cmd->payload.device.tx_power = entry->tx_power;
    cmd->payload.device.company_id = entry->company_id;
    strncpy(cmd->payload.device.vendor, entry->vendor, sizeof(cmd->payload.device.vendor) - 1);
    cmd->payload.device.vendor[sizeof(cmd->payload.device.vendor) - 1] = '\0';
}

static void enqueue_device_command(const command_t *cmd)
{
    if (!g_espnow_queue)
    {
        ESP_LOGW(TAG, "ESP-NOW queue unavailable, dropping device");
        return;
    }

    if (xQueueSend(g_espnow_queue, cmd, 0) != pdTRUE)
    {
        ESP_LOGW(TAG, "ESP-NOW queue full, dropping device");
    }
}

/*
 * send_device_over_espnow() - fire a packed command_t over ESP-NOW to the
 * attack ESP32.  Errors are returned so espnow_forward_task() can retry.
 */
static esp_err_t send_device_over_espnow(const command_t *cmd)
{
    esp_err_t ret = esp_now_send(s_attack_mac, (const uint8_t *)cmd, sizeof(*cmd));

    scanner_mutex_lock();
    bool show_table = g_show_table;
    scanner_mutex_unlock();

    if (ret == ESP_OK)
    {
#if SCANNER_DEBUG
        if (!show_table)
        {
            ESP_LOGI(TAG, "ESP-NOW sent device %02x:%02x:%02x:%02x:%02x:%02x",
                     cmd->payload.device.bda[0], cmd->payload.device.bda[1],
                     cmd->payload.device.bda[2], cmd->payload.device.bda[3],
                     cmd->payload.device.bda[4], cmd->payload.device.bda[5]);
        }
#endif
    }
    else
    {
        ESP_LOGW(TAG, "ESP-NOW send failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

static void espnow_forward_task(void *arg)
{
    (void)arg;
    command_t cmd;

    while (1)
    {
        if (xQueueReceive(g_espnow_queue, &cmd, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        for (int attempt = 0; attempt < ESPNOW_SEND_MAX_ATTEMPTS; attempt++)
        {
            esp_err_t ret = send_device_over_espnow(&cmd);
            if (ret == ESP_OK)
            {
                break;
            }

            if (attempt + 1 < ESPNOW_SEND_MAX_ATTEMPTS)
            {
                vTaskDelay(pdMS_TO_TICKS(ESPNOW_RETRY_DELAY_MS));
            }
            else
            {
                ESP_LOGW(TAG, "ESP-NOW dropped device after %d attempts", ESPNOW_SEND_MAX_ATTEMPTS);
            }
        }
    }
}

/* ── BLE scan result handler ─────────────────────────────────────────────── */

static void log_ble_scan_result(esp_ble_gap_cb_param_t *param)
{
    char bda_str[18];
    uint8_t name_len = 0;
    uint8_t *name_ptr = NULL;
    command_t cmd;
    bool should_enqueue = false;
    bool is_new = false;
    bool show_table = true;
    int8_t rssi = 0;
    char name_copy[ESP_BT_GAP_MAX_BDNAME_LEN + 1] = {0};
    device_eviction_log_t eviction_log = {0};

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

    uint8_t *scan_rsp = param->scan_rst.ble_adv + param->scan_rst.adv_data_len;

    /* Shared scanner state is protected while this callback updates g_devices and flags. */
    scanner_mutex_lock();
    device_entry_t *entry = find_or_create_device(param->scan_rst.bda, &eviction_log);
    if (!entry)
    {
        scanner_mutex_unlock();
        ESP_LOGW(TAG, "BLE device table full, dropping %s",
                 bda2str(param->scan_rst.bda, bda_str, sizeof(bda_str)));
        return;
    }

    is_new = entry->last_seen_ms == 0;

    if (name_ptr && name_len > 0)
    {
        update_entry_name(entry, name_ptr, name_len);
    }

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

    if (is_new)
    {
        g_new_device_blink_pending = true;
    }

    show_table = g_show_table;
    rssi = entry->rssi;
    if (entry->bdname_len > 0)
    {
        memcpy(name_copy, entry->bdname, entry->bdname_len);
        name_copy[entry->bdname_len] = '\0';
        sanitize_name((uint8_t *)name_copy, strlen(name_copy));
    }
    else
    {
        strncpy(name_copy, "(unknown)", sizeof(name_copy) - 1);
    }
    build_device_command_locked(entry, &cmd);
    should_enqueue = true;
    scanner_mutex_unlock();

    log_device_eviction(&eviction_log);

    if (!show_table)
    {
        ESP_LOGI(TAG, "%s: BLE %s RSSI=%d name=%s",
                 is_new ? "NEW" : "UPD",
                 bda2str(param->scan_rst.bda, bda_str, sizeof(bda_str)),
                 (int)rssi,
                 name_copy);

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

    if (should_enqueue)
    {
        enqueue_device_command(&cmd);
    }
}

static void ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    /* BLE callback state transitions are protected by g_scanner_mutex. */
    switch (event)
    {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS)
        {
            scanner_mutex_lock();
            g_ble_scan_params_ready = true;
            scanner_mutex_unlock();
            ble_scan_start();
        }
        else
        {
            ESP_LOGE(TAG, "BLE scan params failed: %d", param->scan_param_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
    {
        bool should_stop = false;
        scanner_mutex_lock();
        g_ble_scan_start_pending = false;
        if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
        {
            g_ble_scanning = true;
            should_stop = !g_scan_enabled;
        }
        else
        {
            g_ble_scanning = false;
        }
        scanner_mutex_unlock();

        if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGI(TAG, "BLE scan started.");
            if (should_stop)
            {
                ble_scan_stop();
            }
        }
        else
        {
            ESP_LOGE(TAG, "BLE scan start failed: %d", param->scan_start_cmpl.status);
        }
        break;
    }
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
    {
        bool should_start = false;
        scanner_mutex_lock();
        g_ble_scan_stop_pending = false;
        g_ble_scanning = false;
        should_start = g_scan_enabled;
        scanner_mutex_unlock();

        ESP_LOGI(TAG, "BLE scan stopped.");
        if (should_start)
        {
            ble_scan_start();
        }
        break;
    }
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT)
        {
            log_ble_scan_result(param);
        }
        else if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT)
        {
            bool should_start = false;
            scanner_mutex_lock();
            g_ble_scanning = false;
            should_start = g_scan_enabled;
            scanner_mutex_unlock();
            if (should_start)
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
    size_t eir_len = 0;
    esp_bt_gap_dev_prop_t *p;
    command_t cmd;
    bool should_enqueue = false;
    bool is_new = false;
    bool show_table = true;
    char name_copy[ESP_BT_GAP_MAX_BDNAME_LEN + 1] = {0};
    device_eviction_log_t eviction_log = {0};

    for (int i = 0; i < param->disc_res.num_prop; i++)
    {
        p = param->disc_res.prop + i;
        switch (p->type)
        {
        case ESP_BT_GAP_DEV_PROP_COD:
            if (p->val && p->len >= 4)
            {
                memcpy(&cod, p->val, sizeof(cod));
            }
            else
            {
                ESP_LOGW(GAP_TAG, "Skipping malformed CoD property len=%d", (int)p->len);
            }
            break;
        case ESP_BT_GAP_DEV_PROP_RSSI:
            if (p->val && p->len >= 1)
            {
                rssi = *(int8_t *)(p->val);
            }
            else
            {
                ESP_LOGW(GAP_TAG, "Skipping malformed RSSI property len=%d", (int)p->len);
            }
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

    /* Shared scanner state is protected while this callback updates m_dev_info and g_devices. */
    scanner_mutex_lock();
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
        if (bdname_len > sizeof(p_dev->bdname) - 1)
        {
            bdname_len = sizeof(p_dev->bdname) - 1;
        }
        memcpy(p_dev->bdname, bdname, bdname_len);
        p_dev->bdname[bdname_len] = '\0';
        p_dev->bdname_len = bdname_len;
    }
    if (eir_len > 0)
    {
        if (eir_len > sizeof(p_dev->eir))
        {
            eir_len = sizeof(p_dev->eir);
        }
        memcpy(p_dev->eir, eir, eir_len);
        p_dev->eir_len = (uint8_t)eir_len;
    }

    if (p_dev->bdname_len == 0)
    {
        get_name_from_eir(p_dev->eir, p_dev->bdname, &p_dev->bdname_len);
    }
    if (p_dev->bdname_len > 0)
    {
        sanitize_name(p_dev->bdname, p_dev->bdname_len);
    }

    device_entry_t *entry = find_or_create_device(param->disc_res.bda, &eviction_log);
    if (!entry)
    {
        p_dev->state = APP_GAP_STATE_DEVICE_DISCOVER_COMPLETE;
        scanner_mutex_unlock();
        ESP_LOGW(GAP_TAG, "Classic device table full, dropping %s",
                 bda2str(param->disc_res.bda, bda_str, sizeof(bda_str)));
        return;
    }

    is_new = entry->last_seen_ms == 0;
    entry->last_seen_ms = get_now_ms();
    entry->rssi = rssi;
    entry->cod = cod;
    entry->type = 0;
    if (p_dev->bdname_len > 0)
    {
        update_entry_name(entry, p_dev->bdname, p_dev->bdname_len);
    }

    if (is_new)
    {
        g_new_device_blink_pending = true;
    }

    show_table = g_show_table;
    if (entry->bdname_len > 0)
    {
        memcpy(name_copy, entry->bdname, entry->bdname_len);
        name_copy[entry->bdname_len] = '\0';
        sanitize_name((uint8_t *)name_copy, strlen(name_copy));
    }
    else
    {
        strncpy(name_copy, "(unknown)", sizeof(name_copy) - 1);
    }
    build_device_command_locked(entry, &cmd);
    should_enqueue = true;

    p_dev->state = APP_GAP_STATE_DEVICE_DISCOVER_COMPLETE;
    scanner_mutex_unlock();

    log_device_eviction(&eviction_log);

    if (!show_table)
    {
        ESP_LOGI(GAP_TAG, "%s: Classic %s RSSI=%d COD=0x%" PRIx32 " name=%s",
                 is_new ? "NEW" : "UPD",
                 bda2str(param->disc_res.bda, bda_str, sizeof(bda_str)),
                 (int)rssi,
                 cod,
                 name_copy);
    }

    if (should_enqueue)
    {
        enqueue_device_command(&cmd);
    }
}

static void bt_app_gap_init(void)
{
    scanner_mutex_lock();
    memset(&m_dev_info, 0, sizeof(app_gap_cb_t));

    m_dev_info.state = APP_GAP_STATE_IDLE;
    scanner_mutex_unlock();
}

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    char bda_str[18];
    char uuid_str[37];

    /* Classic GAP callback state transitions are protected by g_scanner_mutex. */
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
            bool should_start = false;
            scanner_mutex_lock();
            g_classic_discovering = false;
            should_start = g_scan_enabled;
            scanner_mutex_unlock();

            ESP_LOGI(GAP_TAG, "Device discovery stopped.");
            if (should_start)
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
            bool should_stop = false;
            scanner_mutex_lock();
            g_scan_cycle_count++;
            g_classic_discovering = true;
            should_stop = !g_scan_enabled;
            scanner_mutex_unlock();

            ESP_LOGI(GAP_TAG, "Discovery started.");
            if (should_stop)
            {
                classic_scan_stop();
            }
        }
        break;
    }
    case ESP_BT_GAP_RMT_SRVCS_EVT:
    {
        esp_bd_addr_t service_bda = {0};
        bool service_event_matches = false;

        scanner_mutex_lock();
        if (memcmp(param->rmt_srvcs.bda, m_dev_info.bda, ESP_BD_ADDR_LEN) == 0 &&
            m_dev_info.state == APP_GAP_STATE_SERVICE_DISCOVERING)
        {
            m_dev_info.state = APP_GAP_STATE_SERVICE_DISCOVER_COMPLETE;
            memcpy(service_bda, m_dev_info.bda, ESP_BD_ADDR_LEN);
            service_event_matches = true;
        }
        scanner_mutex_unlock();

        if (service_event_matches)
        {
            if (param->rmt_srvcs.stat == ESP_BT_STATUS_SUCCESS)
            {
                ESP_LOGI(GAP_TAG, "Services for device %s found", bda2str(service_bda, bda_str, sizeof(bda_str)));
                for (int i = 0; i < param->rmt_srvcs.num_uuids; i++)
                {
                    esp_bt_uuid_t *u = param->rmt_srvcs.uuid_list + i;
                    ESP_LOGI(GAP_TAG, "--%s", uuid2str(u, uuid_str, 37));
                }
            }
            else
            {
                ESP_LOGI(GAP_TAG, "Services for device %s not found", bda2str(service_bda, bda_str, sizeof(bda_str)));
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

static bool bt_app_gap_start_up(void)
{
    /* register GAP callback function */
    esp_err_t ret = esp_bt_gap_register_callback(bt_app_gap_cb);
    if (ret != ESP_OK)
    {
        ESP_LOGE(GAP_TAG, "Classic GAP callback register failed: %s", esp_err_to_name(ret));
        return false;
    }

    char *dev_name = "ESP_GAP_INQUIRY";
    ret = esp_bt_gap_set_device_name(dev_name);
    if (ret != ESP_OK)
    {
        ESP_LOGW(GAP_TAG, "Set Classic device name failed: %s", esp_err_to_name(ret));
    }

    scanner_mutex_lock();
    bool active_scan = g_ble_active_scan;
    scanner_mutex_unlock();
    (void)set_scanner_discoverability(active_scan);

    /* initialize device information and status */
    bt_app_gap_init();

    /* start to discover nearby Bluetooth devices */
    classic_scan_start();
    return true;
}

static void ble_set_scan_type(bool active)
{
    scanner_mutex_lock();
    g_ble_active_scan = active;
    g_ble_scan_params_ready = false;
    bool was_scanning = g_ble_scanning;
    scanner_mutex_unlock();

    // Stop any running scan
    if (was_scanning)
    {
        esp_err_t stop_ret = esp_ble_gap_stop_scanning();
        if (stop_ret == ESP_OK)
        {
            scanner_mutex_lock();
            g_ble_scan_stop_pending = true;
            scanner_mutex_unlock();
        }
        else if (stop_ret != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGW(TAG, "BLE scan stop before type change failed: %s", esp_err_to_name(stop_ret));
        }
        // Wait for stop to complete - a small delay is sufficient
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // Update the parameter
    esp_ble_scan_params_t scan_params;
    scanner_mutex_lock();
    ble_scan_params.scan_type = active ? BLE_SCAN_TYPE_ACTIVE : BLE_SCAN_TYPE_PASSIVE;
    scan_params = ble_scan_params;
    scanner_mutex_unlock();

    // Re-register and start when ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT fires
    esp_err_t ret = esp_ble_gap_set_scan_params(&scan_params);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "BLE scan params set for type change failed: %s", esp_err_to_name(ret));
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

    scanner_mutex_lock();
    esp_ble_scan_params_t scan_params = ble_scan_params;
    scanner_mutex_unlock();

    ret = esp_ble_gap_set_scan_params(&scan_params);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "BLE scan params set failed: %s", esp_err_to_name(ret));
    }
}

static void dashboard_task(void *arg)
{
    (void)arg;
    char bda_str[18];
    static device_entry_t dashboard_snapshot[MAX_TRACKED_DEVICES];

    /* Fixed-width columns */
    const int COL_MAC = 17;
    const int COL_TYPE = 7;
    const int COL_VENDOR = 10;
    const int COL_COD = 8;
    const int COL_RSSI = 5;
    const int COL_AGE = 10;

    /* Maximum allowed expansion for the Name column to prevent terminal wrapping */
    const int MAX_NAME_LIMIT = 30;

    while (1)
    {
        /* Dashboard copies shared scanner state while g_scanner_mutex is held. */
        scanner_mutex_lock();
        bool show_table = g_show_table;
        uint32_t scan_cycle_count = g_scan_cycle_count;
        bool ble_active_scan = g_ble_active_scan;
        if (show_table)
        {
            memcpy(dashboard_snapshot, g_devices, sizeof(dashboard_snapshot));
        }
        scanner_mutex_unlock();

        if (show_table)
        {
            uint32_t now_ms = get_now_ms();
            uint32_t displayed_rows = 0;
            uint32_t hidden_rows = 0;

            /* 1. Calculate Dynamic Column Width for "Name" */
            int COL_NAME = 4; // Start with width of header "Name"

            for (int i = 0; i < MAX_TRACKED_DEVICES; i++)
            {
                device_entry_t *entry = &dashboard_snapshot[i];
                if (!entry->in_use)
                    continue;

                // Only consider devices seen within the last 2 minutes
                if ((now_ms - entry->last_seen_ms) < 120000)
                {
                    int name_len = (entry->bdname_len > 0) ? (int)strlen((char *)entry->bdname) : 9; // 9 for "(unknown)"
                    if (name_len > COL_NAME)
                    {
                        COL_NAME = name_len;
                    }
                }
            }

            // Apply the cap to ensure the box doesn't exceed screen width
            if (COL_NAME > MAX_NAME_LIMIT)
            {
                COL_NAME = MAX_NAME_LIMIT;
            }

            /* 2. Build the Header based on dynamic width */
            char header[256];
            snprintf(header, sizeof(header),
                     "%-*s %-*s %-*s %-*s %*s %*s %-*s",
                     COL_MAC, "MAC",
                     COL_TYPE, "Type",
                     COL_VENDOR, "Vendor",
                     COL_COD, "Class",
                     COL_RSSI, "RSSI",
                     COL_AGE, "Last Seen",
                     COL_NAME, "Name");

            int content_width = (int)strlen(header);
            int box_width = content_width + 2;

            /* 3. Render Screen */
            printf("\033[2J\033[3J\033[H");

            // Top border
            printf("┌");
            for (int i = 0; i < box_width; i++)
                printf("─");
            printf("┐\n");

            // Scan indicator
            printf("│ Scan #%-3" PRIu32 " - %-*s │\n",
                   scan_cycle_count,
                   content_width - 12,
                   ble_active_scan ? "ACTIVE (names/UUIDs)" : "PASSIVE (stealth)");

            // Header row
            printf("│ %s │\n", header);

            // Divider
            printf("├");
            for (int i = 0; i < box_width; i++)
                printf("─");
            printf("┤\n");

            /* 4. Device rows */
            for (int i = 0; i < MAX_TRACKED_DEVICES; i++)
            {
                device_entry_t *entry = &dashboard_snapshot[i];
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

                char display_name[ESP_BT_GAP_MAX_BDNAME_LEN + 1] = {0};
                if (entry->bdname_len > 0)
                {
                    size_t display_name_len = entry->bdname_len;
                    if (display_name_len > sizeof(display_name) - 1)
                    {
                        display_name_len = sizeof(display_name) - 1;
                    }
                    memcpy(display_name, entry->bdname, display_name_len);
                    display_name[display_name_len] = '\0';
                    sanitize_name((uint8_t *)display_name, display_name_len);
                }
                else
                {
                    strncpy(display_name, "(unknown)", sizeof(display_name) - 1);
                }
                const char *name = display_name;

                char age_str[16];
                snprintf(age_str, sizeof(age_str), "%" PRIu32 "s", age_ms / 1000);

                /* CoD labels are resolved by the CrowPanel dashboard from an SD-card database.  This scanner only collects raw data. */
                char class_str[16];
                if (entry->cod != 0)
                {
                    snprintf(class_str, sizeof(class_str), "0x%06" PRIx32, entry->cod);
                }
                else
                {
                    snprintf(class_str, sizeof(class_str), "BLE");
                }

                /*
                 * The fix: using %-*.*s
                 * This provides both minimum padding and maximum truncation.
                 */
                printf("%s│ %-*s %-*s %-*.*s %-*s %*d %*s %-*.*s │" A_RST "\n",
                       row_color,
                       COL_MAC, bda2str(entry->bda, bda_str, sizeof(bda_str)),
                       COL_TYPE, type,
                       COL_VENDOR, COL_VENDOR, entry->vendor, // Truncate Vendor if too long
                       COL_COD, class_str,
                       COL_RSSI, (int)entry->rssi,
                       COL_AGE, age_str,
                       COL_NAME, COL_NAME, name); // Truncate Name to dynamic limit

                displayed_rows++;
            }

            /* 5. Empty-state & Footer */
            if (displayed_rows == 0)
            {
                printf("│ %-*s │\n", content_width, "(no devices in range)");
            }

            if (hidden_rows > 0)
            {
                char hidden_msg[128];
                snprintf(hidden_msg, sizeof(hidden_msg),
                         "... and %" PRIu32 " more device(s) - type 'table off' for full list.",
                         hidden_rows);
                printf("│ %-*s │\n", content_width, hidden_msg);
            }

            printf("├");
            for (int i = 0; i < box_width; i++)
                printf("─");
            printf("┤\n");

            printf("│ %-*s │\n", content_width,
                   "[table on|off] [start|stop] [active|passive]");

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
    (void)arg;
    char buf[32];
    while (1)
    {
        if (fgets(buf, sizeof(buf), stdin))
        {
            if (strncmp(buf, "stop", 4) == 0)
            {
                /* Command task updates shared scanner flags while g_scanner_mutex is held. */
                scanner_mutex_lock();
                g_scan_enabled = false;
                scanner_mutex_unlock();
                classic_scan_stop();
                ble_scan_stop();
                printf("Scan stopped.\n");
            }
            else if (strncmp(buf, "table on", 8) == 0)
            {
                scanner_mutex_lock();
                g_show_table = true;
                scanner_mutex_unlock();
            }
            else if (strncmp(buf, "table off", 9) == 0)
            {
                scanner_mutex_lock();
                g_show_table = false;
                scanner_mutex_unlock();
                printf("\033[2J\033[H");
                printf("Raw log view enabled.\n");
            }
            else if (strncmp(buf, "start", 5) == 0)
            {
                scanner_mutex_lock();
                g_scan_enabled = true;
                scanner_mutex_unlock();
                classic_scan_start();
                ble_scan_start();
                printf("Scan started.\n");
            }
            else if (strncmp(buf, "active", 6) == 0)
            {
                ble_set_scan_type(true);
                (void)set_scanner_discoverability(true);
                printf("BLE scan: ACTIVE  (requests names & UUIDs - visible)\n");
            }
            else if (strncmp(buf, "passive", 7) == 0)
            {
                ble_set_scan_type(false);
                (void)set_scanner_discoverability(false);
                printf("BLE scan: PASSIVE (listen‑only - stealth)\n");
            }
            else if (buf[0] != '\n' && buf[0] != '\r' && buf[0] != '\0')
            {
                scanner_mutex_lock();
                bool was_showing = g_show_table;
                g_show_table = false;
                scanner_mutex_unlock();

                printf("\nUnknown command: %s\n", buf);
                printf("Available commands:\n");
                printf("  start           - start scanning\n");
                printf("  stop            - stop scanning\n");
                printf("  table on        - show table view (paginated)\n");
                printf("  table off       - show raw log view (no pagination)\n");
                printf("  active          - active BLE scan (requests names/UUIDs)\n");
                printf("  passive         - passive BLE scan (listen-only)\n");
                printf("\nPress Enter to return to dashboard...\n");

                /* Consume a whole line - safe for arrow keys and other
                 * multi‑byte sequences that would otherwise corrupt the
                 * next fgets() read. */
                char dummy[8];
                fgets(dummy, sizeof(dummy), stdin);

                if (was_showing)
                {
                    scanner_mutex_lock();
                    g_show_table = true;
                    scanner_mutex_unlock();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ── ESP-NOW initialisation ─────────────────────────────────────────────── */

/*
 * espnow_send_cb() - fire-and-forget send callback.
 * Errors are visible in the log from send_device_over_espnow(); no
 * additional action is required here.
 */
static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
    /* ESP-NOW callback reads shared display state while g_scanner_mutex is held. */
    scanner_mutex_lock();
    bool show_table = g_show_table;
    scanner_mutex_unlock();

    if (status != ESP_NOW_SEND_SUCCESS && !show_table)
    {
        ESP_LOGW(TAG, "ESP-NOW send callback: delivery failed");
    }
}

/*
 * espnow_init() - bring up Wi-Fi STA + ESP-NOW and register the attack
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
        /* ESP_ERR_INVALID_STATE means the loop already exists - that is fine */
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
    /* Wi-Fi channel 1 (2412 MHz) - avoids interference with Bluetooth which hops across 2402-2480 MHz. */
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

    /* Register attack ESP32 (78:1c:3c:a5:a8:d2) as peer */
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memcpy(peer.peer_addr, s_attack_mac, sizeof(peer.peer_addr));
    peer.channel = ESPNOW_CHANNEL;
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
    /* Initialize NVS - it is used to store PHY calibration data and save key-value pairs in flash memory*/
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    scan_led_init();
    ESP_LOGI(TAG, "Free internal heap: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "Free PSRAM heap: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    g_scanner_mutex = xSemaphoreCreateMutex();
    if (!g_scanner_mutex)
    {
        ESP_LOGE(TAG, "Failed to create scanner mutex");
        return;
    }

    g_espnow_queue = xQueueCreate(ESPNOW_QUEUE_DEPTH, sizeof(command_t));
    if (!g_espnow_queue)
    {
        ESP_LOGE(TAG, "Failed to create ESP-NOW queue");
        return;
    }

    bool bt_controller_initialized = false;
    bool bt_controller_enabled = false;
    bool bluedroid_initialized = false;
    bool bluedroid_enabled = false;

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK)
    {
        ESP_LOGE(GAP_TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        goto cleanup;
    }
    bt_controller_initialized = true;

    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM)) != ESP_OK)
    {
        ESP_LOGE(GAP_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        goto cleanup;
    }
    bt_controller_enabled = true;

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg)) != ESP_OK)
    {
        ESP_LOGE(GAP_TAG, "%s initialize bluedroid failed: %s", __func__, esp_err_to_name(ret));
        goto cleanup;
    }
    bluedroid_initialized = true;

    if ((ret = esp_bluedroid_enable()) != ESP_OK)
    {
        ESP_LOGE(GAP_TAG, "%s enable bluedroid failed: %s", __func__, esp_err_to_name(ret));
        goto cleanup;
    }
    bluedroid_enabled = true;

    ESP_LOGI(GAP_TAG, "Own address:[%s]", bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str, sizeof(bda_str)));

    /* Install UART driver and switch stdin to blocking mode so that
     * fgets() in scan_control_task blocks on each read rather than
     * spinning and returning EOF immediately.                        */
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0));
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

    /* Bring up ESP-NOW (Wi-Fi STA + peer registration) */
    espnow_init();

    /* ESP-NOW forwarder uses 3072 bytes for queued send retries and logging. */
    if (xTaskCreate(espnow_forward_task, "espnow_fwd", 3072, NULL, 1, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create task: espnow_fwd");
        return;
    }

    if (!bt_app_gap_start_up())
    {
        return;
    }
    ble_app_gap_start_up();

    /* LED task uses 1024 bytes; it only toggles GPIO and checks shared flags. */
    if (xTaskCreate(scan_led_task, "scan_led", 1024, NULL, 1, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create task: scan_led");
        return;
    }

    /* Command task uses 3072 bytes for blocking UART fgets() and command logging. */
    if (xTaskCreate(scan_control_task, "scan_ctrl", 3072, NULL, 1, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create task: scan_ctrl");
        return;
    }

    /* Dashboard task uses 4096 bytes for repeated printf/snprintf rendering. */
    if (xTaskCreate(dashboard_task, "dashboard", 4096, NULL, 1, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create task: dashboard");
        return;
    }
    return;

cleanup:
    if (bluedroid_enabled)
    {
        esp_err_t cleanup_ret = esp_bluedroid_disable();
        if (cleanup_ret != ESP_OK)
        {
            ESP_LOGW(GAP_TAG, "Bluedroid disable cleanup failed: %s", esp_err_to_name(cleanup_ret));
        }
    }
    if (bluedroid_initialized)
    {
        esp_err_t cleanup_ret = esp_bluedroid_deinit();
        if (cleanup_ret != ESP_OK)
        {
            ESP_LOGW(GAP_TAG, "Bluedroid deinit cleanup failed: %s", esp_err_to_name(cleanup_ret));
        }
    }
    if (bt_controller_enabled)
    {
        esp_err_t cleanup_ret = esp_bt_controller_disable();
        if (cleanup_ret != ESP_OK)
        {
            ESP_LOGW(GAP_TAG, "BT controller disable cleanup failed: %s", esp_err_to_name(cleanup_ret));
        }
    }
    if (bt_controller_initialized)
    {
        esp_err_t cleanup_ret = esp_bt_controller_deinit();
        if (cleanup_ret != ESP_OK)
        {
            ESP_LOGW(GAP_TAG, "BT controller deinit cleanup failed: %s", esp_err_to_name(cleanup_ret));
        }
    }
}

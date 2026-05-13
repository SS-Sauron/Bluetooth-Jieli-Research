/*
 * AVRCP Runtime Command Console — Bruce-style Menu Edition
 * Target: user-selected Bluetooth AVRCP device
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
#include "driver/gpio.h"
#include "espnow_proto.h"

#include "menu.h" /* ← pulls in extern declarations + menu_run_main() */

#define TAG "AVRCP"

#define STATUS_LED_GPIO GPIO_NUM_2
#define STATUS_LED_ON_LEVEL 1
#define STATUS_LED_OFF_LEVEL 0

/* ── Runtime target address — non-const, required by L2CAP API ──────── */
/* No default target is set. Pick one from Device List or, in a future
 * command, set it with: default XX:XX:XX:XX:XX:XX. */
esp_bd_addr_t g_target_addr = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* ── Global L2CAP state ──────────────────────────────────────────────── */
SemaphoreHandle_t g_l2cap_sem = NULL;
SemaphoreHandle_t g_acl_disc_sem = NULL;
int g_l2cap_fd = -1;
static QueueHandle_t g_espnow_queue = NULL;

/* ── Runtime command parameters ─────────────────────────────────────── */
volatile uint8_t g_avrcp_opcode = 0x41;
volatile int g_repeats = 0;
volatile bool g_abort = false;

static void status_led_set(bool connected)
{
    gpio_set_level(STATUS_LED_GPIO,
                   connected ? STATUS_LED_ON_LEVEL : STATUS_LED_OFF_LEVEL);
}

static void status_led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << STATUS_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    status_led_set(false);
}

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
        status_led_set(g_l2cap_fd >= 0);
        xSemaphoreGive(g_l2cap_sem);
        break;
    case ESP_BT_L2CAP_CLOSE_EVT:
        g_l2cap_fd = -1;
        status_led_set(false);
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
    {
        status_led_set(false);
        return;
    }

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
    status_led_set(false);
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

/* ── Bruce-pattern input task ────────────────────────────────────────
 * Reads raw keystrokes from stdin and sets global input flags.
 * Runs forever at priority 1.  The menu system calls check() which
 * atomically reads and clears these flags.
 * ─────────────────────────────────────────────────────────────────── */
void input_task(void *arg)
{
    (void)arg;
    while (1)
    {
        int ch = getchar();
        if (ch == EOF)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* ── Arrow key escape sequences ────────────────────────────────
         * VT100: ESC [ A/B/C/D  =  up/down/right/left
         * We poll with a short timeout to see if the next bytes are
         * the bracket + arrow code.  If not, treat ESC as Back.
         * ────────────────────────────────────────────────────────────── */
        if (ch == 27)
        { /* ESC */
            /* Poll for '[' within 50 ms */
            int c2 = EOF;
            for (int i = 0; i < 10; i++)
            {
                vTaskDelay(pdMS_TO_TICKS(5));
                c2 = getchar();
                if (c2 != EOF)
                    break;
            }
            if (c2 == '[')
            {
                /* Poll for the arrow letter */
                int c3 = EOF;
                for (int i = 0; i < 10; i++)
                {
                    vTaskDelay(pdMS_TO_TICKS(5));
                    c3 = getchar();
                    if (c3 != EOF)
                        break;
                }
                switch (c3)
                {
                case 'A': /* Up */
                    g_up_press = true;
                    break;
                case 'B': /* Down */
                    g_down_press = true;
                    break;
                case 'C': /* Right → treat as Select */
                    g_sel_press = true;
                    break;
                case 'D': /* Left → treat as Back */
                    g_esc_press = true;
                    break;
                default: /* Unknown — treat ESC as Back */
                    g_esc_press = true;
                    break;
                }
            }
            else
            {
                /* ESC followed by something else → Back */
                g_esc_press = true;
            }
            g_any_press = true;
            continue;
        }

        /* ── Regular keys ──────────────────────────────────────────── */
        switch (ch)
        {
        case 'w':
        case 'W':
            g_up_press = true;
            break;
        case 's':
        case 'S':
            g_down_press = true;
            break;
        case 'a':
        case 'A':
            g_esc_press = true;
            break;
        case 'd':
        case 'D':
        case '\r':
        case '\n':
            g_sel_press = true;
            break;
        case 'q':
        case 'Q':
        case 'r':
        case 'R':
            g_reboot_press = true;
            break;
        case '1' ... '9':
            g_direct_pick = (uint8_t)(ch - '0');
            break;
        default:
            break;
        }
        if (ch == 'w' || ch == 'W' || ch == 's' || ch == 'S' ||
            ch == 'a' || ch == 'A' || ch == 'd' || ch == 'D' ||
            ch == '\r' || ch == '\n' || ch == 'q' || ch == 'Q' ||
            ch == 'r' || ch == 'R' || (ch >= '1' && ch <= '9'))
        {
            g_any_press = true;
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

/* ── Bluetooth address to string ───────────────────────────────────── */
char *bda2str(esp_bd_addr_t bda, char *str, size_t size)
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
    status_led_init();

    /* ── BT controller + Bluedroid ── */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    /* Suppress low-level BT stack chatter */
    esp_log_level_set("BT_HCI", ESP_LOG_NONE);
    esp_log_level_set("BT_APPL", ESP_LOG_NONE);
    esp_log_level_set("BT_L2CAP", ESP_LOG_NONE);
    esp_log_level_set("BT_BTM", ESP_LOG_NONE);

    const uint8_t *mac = esp_bt_dev_get_address();
    ESP_LOGI(TAG, "ESP32 BT MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* ── Semaphores ── */
    g_l2cap_sem = xSemaphoreCreateBinary();
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
    vTaskDelay(pdMS_TO_TICKS(200));

    /* ── Spawn the input task (sets global flags from keystrokes) ── */
    xTaskCreate(input_task, "input", 2048, NULL, 1, NULL);

    /* ── Init ESP‑NOW mesh ── */
    espnow_init();

    /* ── Hand off to the Bruce-style menu — never returns ── */
    menu_run_main();
}

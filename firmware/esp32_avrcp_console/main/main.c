/*
 * AVRCP Runtime Command Console — Multi‑Target, Abort, Graceful Exit
 * Target: Soundcore R50i NC (default F4:B6:2D:AE:AB:E0)
 * Uses ESP32 real MAC (no spoofing)
 *
 * Available commands (type and press Enter):
 *   connect [MAC]  – Establish an L2CAP/ACL link to earbuds (optional MAC)
 *   up [N]         – Volume Up, N presses (default 15, 0 = do nothing)
 *   down [N]       – Volume Down, N presses (default 15, 0 = do nothing)
 *   exit           – Stop any running volume batch (like Ctrl+C)
 *   disconnect     – Gracefully hang up the ACL link (phone‑style "hang up")
 *   reboot         – Force disconnect and restart ESP32 (like Ctrl+\ or SIGQUIT)
 *   help           – Show this list
 *
 * Three‑command mental model (inspired by Linux terminal):
 *   • exit        → interrupts the current operation, but keeps the connection alive
 *                   analogous to pressing Ctrl+C in a terminal (SIGINT)
 *   • reboot      → forcefully terminates the entire firmware and restarts the chip,
 *                   analogous to pressing Ctrl+\ (SIGQUIT) or a system reset
 *   • disconnect  → cleanly closes the Bluetooth connection without rebooting,
 *                   analogous to hanging up a phone call; you can reconnect later
 *
 * The command table (command_t s_commands[]) is extensible:
 * add one line to register a new AVRCP operation.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_l2cap_bt_api.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_console.h"

#define TAG "AVRCP"

/* ── Default target earbuds (non‑const, required by L2CAP API) ── */
static esp_bd_addr_t g_target_addr = {
    0xF4, 0xB6, 0x2D, 0xAE, 0xAB, 0xE0};

/* ── Global L2CAP state ───────────────────────────────────
 * g_l2cap_fd == -1  ⇒ no active connection
 * All volume commands are guarded by this condition.
 * ──────────────────────────────────────────────────────── */
static SemaphoreHandle_t g_l2cap_sem = NULL;
static SemaphoreHandle_t g_acl_disc_sem = NULL;
static int g_l2cap_fd = -1;

/* ── Runtime command parameters ───────────────────────────
 * Both are set by the command handler and read atomically
 * by the main loop.  s_repeats == 0 ⇒ idle.
 * ──────────────────────────────────────────────────────── */
static volatile uint8_t g_avrcp_opcode = 0x41; /* 0x41 = Vol Up, 0x42 = Vol Down */
static volatile int g_repeats = 0;             /* 0 ⇒ wait for command */
static volatile bool g_abort = false;          /* set by exit handler to cancel batch */

/* ── Command table ────────────────────────────────────────
 * Each entry maps a user string to an AVRCP opcode and
 * default repeat count.  To add a new AVRCP operation,
 * just insert one line into this table.
 * ──────────────────────────────────────────────────────── */
typedef struct
{
    const char *name;
    uint8_t opcode;
    int default_reps;
    const char *description;
} command_t;

static const command_t s_commands[] = {
    {"up", 0x41, 15, "Volume Up   [N times] (0 = do nothing)"},
    {"down", 0x42, 15, "Volume Down [N times] (0 = do nothing)"},
};
#define NUM_COMMANDS (sizeof(s_commands) / sizeof(s_commands[0]))

/* =========================================================
 * read_line — stable line reader (no ANSI escapes)
 * ---------------------------------------------------------
 * Reads characters one at a time until '\n'.  Supports
 * backspace and ignores carriage return.  Characters are
 * echoed immediately for visibility.
 * Returns true if a non‑empty line was captured.
 * ========================================================= */
static bool read_line(char *buf, size_t len)
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
 * Builds an 8‑byte AVRCP Passthrough frame and writes it
 * to the L2CAP file descriptor.  Returns void; errors are
 * logged via ESP_LOGE.
 * ========================================================= */
static void send_avrcp_passthrough(int fd, uint8_t tl,
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
 * ---------------------------------------------------------
 * Handles INIT, VFS_REGISTER, OPEN, and CLOSE events from
 * the L2CAP module.  Signals g_l2cap_sem to unblock the
 * connect sequence.
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
        break;
    default:
        break;
    }
}

/* =========================================================
 * GAP callback
 * ---------------------------------------------------------
 * Listens for ACL disconnection events.  Used by the
 * disconnect / exit commands to confirm a clean tear‑down
 * before restarting or reconnecting.
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
 * Core connection logic shared by the "connect" command.
 * Handles L2CAP init, VFS registration, and outgoing
 * connection to the target MAC.  On failure, cleans up and
 * returns an error string.  On success, g_l2cap_fd is set.
 * ========================================================= */
static esp_err_t do_connect(esp_bd_addr_t addr, const char **err_str)
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
 * Core disconnect logic.  Tears down L2CAP and waits for
 * the ACL link to be fully released.  Callable even when
 * already disconnected (safe no‑op).
 * ========================================================= */
static void do_disconnect(void)
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

/* =========================================================
 * connect_handler
 * ---------------------------------------------------------
 * Usage:  connect [MAC]
 * Parses an optional MAC address; if none given, uses the
 * default target.  Calls do_connect() and reports the result.
 * ========================================================= */
static int connect_handler(int argc, char **argv)
{
    if (g_l2cap_fd >= 0)
    {
        printf("Already connected. Use 'disconnect' first.\n");
        return 0;
    }

    esp_bd_addr_t addr;
    if (argc > 1)
    {
        /* parse user-provided MAC */
        if (sscanf(argv[1], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &addr[0], &addr[1], &addr[2],
                   &addr[3], &addr[4], &addr[5]) != 6)
        {
            printf("Invalid MAC format. Use e.g. F4:B6:2D:AE:AB:E0\n");
            return 0;
        }
    }
    else
    {
        memcpy(addr, g_target_addr, sizeof(esp_bd_addr_t));
    }

    printf("Connecting to %02x:%02x:%02x:%02x:%02x:%02x ...\n",
           addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    const char *err_str = NULL;
    esp_err_t ret = do_connect(addr, &err_str);
    if (ret == ESP_OK)
    {
        printf("Connected successfully.\n");
    }
    else
    {
        printf("Connection failed: %s (0x%x)\n", err_str, ret);
    }
    return 0;
}

/* =========================================================
 * disconnect_handler
 * ---------------------------------------------------------
 * Usage:  disconnect
 * Calls do_disconnect() and reports the result.
 * ========================================================= */
static int disconnect_handler(int argc, char **argv)
{
    if (g_l2cap_fd < 0)
    {
        printf("Not connected.\n");
        return 0;
    }
    do_disconnect();
    printf("Disconnected. Connection closed (like hanging up a phone).\n");
    return 0;
}

/* =========================================================
 * exit_handler
 * ---------------------------------------------------------
 * Usage:  exit
 * Immediately stops any running volume batch.  This is the
 * equivalent of pressing Ctrl+C in a terminal — it interrupts
 * the current command without disconnecting or rebooting.
 * ========================================================= */
static int exit_handler(int argc, char **argv)
{
    g_abort = true;
    g_repeats = 0;
    printf("Batch aborted (like Ctrl+C). Connection is still active.\n");
    return 0;
}

/* =========================================================
 * reboot_handler
 * ---------------------------------------------------------
 * Usage:  reboot
 * Force‑disconnects (if connected) and restarts the ESP32.
 * This is analogous to pressing Ctrl+\ in a terminal — a
 * non‑negotiable termination that leaves the system clean
 * for the next session.
 * ========================================================= */
static int reboot_handler(int argc, char **argv)
{
    ESP_LOGI(TAG, "Rebooting (like Ctrl+\\)...");
    do_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return 0;
}

/* =========================================================
 * volume_handler
 * ---------------------------------------------------------
 * Called when the user types "up [N]" or "down [N]".
 * Sets the global g_avrcp_opcode and g_repeats for the main
 * loop to execute.  Refuses if no L2CAP connection exists.
 * Allows 0 repeats to effectively do nothing.
 * ========================================================= */
static int volume_handler(int argc, char **argv)
{
    if (g_l2cap_fd < 0)
    {
        printf("Not connected. Use 'connect' first.\n");
        return 0;
    }

    const char *cmd = argv[0];
    for (int i = 0; i < (int)NUM_COMMANDS; i++)
    {
        if (strcmp(cmd, s_commands[i].name) == 0)
        {
            g_avrcp_opcode = s_commands[i].opcode;
            g_repeats = s_commands[i].default_reps;
            if (argc > 1)
            {
                int n = atoi(argv[1]);
                if (n >= 0)
                    g_repeats = n; // allow 0 (do nothing)
            }
            printf("Queued: %s x%d\n", s_commands[i].description, g_repeats);
            return 0;
        }
    }
    return 1;
}

/* =========================================================
 * app_main
 * ---------------------------------------------------------
 * Initialises the platform, registers all console commands,
 * and runs the interactive loop.  No connection is attempted
 * until the user types 'connect'.
 * ========================================================= */
void app_main(void)
{
    ESP_LOGI(TAG, "=== AVRCP Console ===");

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

    /*
     * Suppress low‑level Bluetooth stack chatter that otherwise spills into
     * the serial console and interrupts the interactive prompt.  These tags
     * are not relevant to the AVRCP injection logic:
     *
     *  BT_HCI   – Host Controller Interface events (sniff mode, connection
     *             parameters, power management).  Informational only.
     *  BT_APPL  – Application‑layer power‑management status (e.g. attempt
     *             to enter sniff mode).  No impact on data transfer.
     *  BT_L2CAP – L2CAP channel configuration details (FCR negotiation,
     *             MTU, etc.).  Already known to be working.
     *  BT_BTM   – Security Manager access‑request warnings (remote features
     *             unknown).  Does not prevent the connection.
     */
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

    /* ── Register callbacks ──
     * Must be done after Bluedroid is enabled and before any connection
     * attempt, so the callbacks are in place when the commands are used.
     */
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_callback));
    ESP_ERROR_CHECK(esp_bt_l2cap_register_callback(l2cap_callback));

    /* ── Console setup ── */
    esp_console_config_t console_cfg = ESP_CONSOLE_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_init(&console_cfg));
    ESP_ERROR_CHECK(esp_console_register_help_command());

    /* Register volume commands */
    for (int i = 0; i < (int)NUM_COMMANDS; i++)
    {
        const esp_console_cmd_t cmd = {
            .command = s_commands[i].name,
            .help = s_commands[i].description,
            .hint = "[N]",
            .func = &volume_handler,
        };
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    }

    /* Register infrastructure commands with descriptive help */
    const esp_console_cmd_t connect_cmd = {
        .command = "connect",
        .help = "Establish a Bluetooth link to earbuds [MAC] (default target used if no MAC given)",
        .hint = "[MAC]",
        .func = &connect_handler};
    const esp_console_cmd_t disconnect_cmd = {
        .command = "disconnect",
        .help = "Gracefully hang up the connection (like ending a phone call); allows reconnecting later",
        .hint = NULL,
        .func = &disconnect_handler};
    const esp_console_cmd_t exit_cmd = {
        .command = "exit",
        .help = "Stop the currently running volume batch (like Ctrl+C); does not disconnect or reboot",
        .hint = NULL,
        .func = &exit_handler};
    const esp_console_cmd_t reboot_cmd = {
        .command = "reboot",
        .help = "Force disconnect and restart ESP32 (like Ctrl+\\ or SIGQUIT); clean slate before reflashing",
        .hint = NULL,
        .func = &reboot_handler};
    ESP_ERROR_CHECK(esp_console_cmd_register(&connect_cmd));
    ESP_ERROR_CHECK(esp_console_cmd_register(&disconnect_cmd));
    ESP_ERROR_CHECK(esp_console_cmd_register(&exit_cmd));
    ESP_ERROR_CHECK(esp_console_cmd_register(&reboot_cmd));

    ESP_LOGI(TAG, "Ready. Type 'connect' to begin.");
    ESP_LOGI(TAG, "Commands: connect, up/down, exit, disconnect, reboot, help");

    /* ── Interactive console loop ── */
    int cmd_ret;
    char line[64];
    while (1)
    {
        /* Execute pending AVRCP command if connected */
        if (g_repeats > 0 && g_l2cap_fd >= 0)
        {
            int count = g_repeats;
            uint8_t op = g_avrcp_opcode;
            g_repeats = 0;

            ESP_LOGI(TAG, "Sending %d %s commands...", count,
                     (op == 0x41) ? "Volume Up" : "Volume Down");

            static uint8_t tl = 0; // persistent across batches
            g_abort = false;
            for (int i = 0; i < count; i++)
            {
                /* Check if exit handler set the abort flag — stops the batch immediately */
                if (g_abort)
                {
                    ESP_LOGI(TAG, "Batch aborted by user.");
                    break;
                }
                send_avrcp_passthrough(g_l2cap_fd, tl, op, 0x00);
                vTaskDelay(pdMS_TO_TICKS(200));
                send_avrcp_passthrough(g_l2cap_fd, tl, op, 0x80);
                vTaskDelay(pdMS_TO_TICKS(500));
                tl = (tl + 1) % 16;
            }
            if (!g_abort)
            {
                ESP_LOGI(TAG, "Done. Type another command.");
            }
        }

        /* Give logging a moment to flush before showing the prompt */
        vTaskDelay(pdMS_TO_TICKS(50));
        printf("avrcp> ");
        fflush(stdout);

        /* Read a complete line (Enter to submit) */
        while (!read_line(line, sizeof(line)))
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        /* Dispatch to the registered console command */
        if (esp_console_run(line, &cmd_ret) == ESP_ERR_NOT_FOUND)
        {
            printf("Unknown command. Type 'help' for available commands.\n");
        }
    }
}
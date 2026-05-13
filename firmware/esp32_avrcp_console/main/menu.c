/*
 * menu.c — Bruce-style loopOptions menu for the AVRCP serial console.
 *
 * Implements:
 *   check()           — reads and clears the next pending input flag
 *   loop_options()    — core navigation engine (polls check(), ANSI render)
 *   action_*()        — leaf action callbacks
 *   submenu_*()       — sub-menu wrappers
 *   menu_run_main()   — top-level entry point
 *
 * Input architecture (Bruce pattern):
 *   input_task() [main.c] sets global flags → check() reads + clears →
 *   loop_options() dispatches.  This file never touches getchar() directly.
 *
 * Adding a new action:
 *   1. Write a static void action_foo(void) function here.
 *   2. Add MENU_OPT("Foo", action_foo) to the relevant opts[] array.
 *   Done — no handler registration, no esp_console glue needed.
 */

#include "menu.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "MENU"

/* ── ANSI escape helpers ────────────────────────────────────────────────
 * Standard VT100; works in idf.py monitor, PuTTY, screen, minicom.
 * ───────────────────────────────────────────────────────────────────── */
#define A_CLEAR "\033[2J\033[H"
#define A_BOLD "\033[1m"
#define A_RST "\033[0m"
#define A_INV "\033[7m"
#define A_CYAN "\033[36m"
#define A_YELLOW "\033[33m"
#define A_GREEN "\033[32m"
#define A_RED "\033[31m"
#define A_WHITE "\033[37m"

/* ── Persistent AVRCP transaction label ─────────────────────────────── */
static uint8_t s_tl = 0;

/* ── Global input flags ─────────────────────────────────────────────────
 * Written exclusively by input_task() (main.c).
 * Read and cleared exclusively by check() and wait_key() (this file).
 * ───────────────────────────────────────────────────────────────────── */
volatile bool g_up_press = false;
volatile bool g_down_press = false;
volatile bool g_sel_press = false;
volatile bool g_esc_press = false;
volatile bool g_reboot_press = false;
volatile bool g_any_press = false;
volatile uint8_t g_direct_pick = 0;

/* ── Remote device table (populated via ESP-NOW) ────────────────────── */
remote_device_t g_remote_devices[MAX_REMOTE_DEVICES] = {0};
uint8_t g_remote_device_count = 0;

/* ── Selected device index for action_select_device() ───────────────── */
static int g_selected_device_index = -1;

/* ── Tracks whether target was just picked from Device List ────────────
 * When true, action_connect() skips the prompt and connects immediately.
 * Cleared after one use. ────────────────────────────────────────────── */
static bool g_target_just_selected = false;

/* ── External helpers from main.c ───────────────────────────────────── */
extern char *bda2str(esp_bd_addr_t bda, char *str, size_t size);

/* ── Lightweight timestamp ───────────────────────────────────────────── */
static inline uint32_t get_now_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static bool target_addr_is_zero(void)
{
    for (size_t i = 0; i < sizeof(g_target_addr); i++)
    {
        if (g_target_addr[i] != 0)
        {
            return false;
        }
    }
    return true;
}

static const char *target_name_for_addr(void)
{
    if (target_addr_is_zero())
    {
        return "(no target)";
    }

    for (int i = 0; i < MAX_REMOTE_DEVICES; i++)
    {
        if (g_remote_devices[i].in_use &&
            memcmp(g_remote_devices[i].bda, g_target_addr, sizeof(esp_bd_addr_t)) == 0)
        {
            return g_remote_devices[i].name[0] ? g_remote_devices[i].name : "(unknown)";
        }
    }
    return "(unknown)";
}

static const char *main_menu_color(int index)
{
    switch (index)
    {
    case 0:
        return A_GREEN;  /* Bluetooth */
    case 1:
    case 2:
        return A_YELLOW; /* Wi-Fi / RFID */
    case 3:
        return A_WHITE;  /* Status */
    case 4:
        return A_RED;    /* Reboot */
    default:
        return A_RST;
    }
}

static bool mesh_recent(void)
{
    uint32_t now_ms = get_now_ms();
    for (int i = 0; i < MAX_REMOTE_DEVICES; i++)
    {
        if (g_remote_devices[i].in_use &&
            (uint32_t)(now_ms - g_remote_devices[i].last_seen_ms) < 10000)
        {
            return true;
        }
    }
    return false;
}

/* ── check() ─────────────────────────────────────────────────────────── *
 *
 * Atomically reads and clears the next pending input flag.
 * Individual flags are cleared BEFORE g_any_press (the sentinel) so
 * input_task() never observes g_any_press=false while a flag is still set.
 *
 * Priority order: reboot > nav flags > direct pick.
 * ───────────────────────────────────────────────────────────────────── */
int check(void)
{
    /* Fast gate — avoid touching individual flags when nothing is pending */
    if (!g_any_press && g_direct_pick == 0)
        return 0;

    /* Reboot: highest priority, clear sentinel immediately */
    if (g_reboot_press)
    {
        g_reboot_press = false; /* individual flag first */
        g_any_press = false;    /* sentinel last         */
        return 99;
    }

    int ret = 0;

    if (g_any_press)
    {
        if (g_up_press)
        {
            g_up_press = false;
            ret = 1;
        }
        else if (g_down_press)
        {
            g_down_press = false;
            ret = 2;
        }
        else if (g_sel_press)
        {
            g_sel_press = false;
            ret = 3;
        }
        else if (g_esc_press)
        {
            g_esc_press = false;
            ret = 4;
        }
        g_any_press = false; /* sentinel cleared last — always */
    }

    /* Direct pick overrides nav return if both arrive simultaneously */
    if (g_direct_pick != 0)
    {
        ret = 4 + (int)g_direct_pick; /* 1-9 maps to 5-13 */
        g_direct_pick = 0;
    }

    return ret;
}

/* ── wait_key() ─────────────────────────────────────────────────────────
 * Yields with 10 ms vTaskDelay until any key arrives from input_task.
 * Clears ALL flags after waking so they do not bleed into the next
 * menu render cycle.
 * ───────────────────────────────────────────────────────────────────── */
static void wait_key(void)
{
    while (!g_any_press && g_direct_pick == 0)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    /* Clear everything — caller only needed "any key", not which one */
    g_up_press = g_down_press = g_sel_press =
        g_esc_press = g_reboot_press = false;
    g_any_press = false;
    g_direct_pick = 0;
}

/* Forward declaration — loop_options() dispatches case 99 to action_reboot */
static void action_reboot(void);

/* ── render_menu() ──────────────────────────────────────────────────────
 * Pure output — no input reads here.
 * ───────────────────────────────────────────────────────────────────── */
static void render_menu(const menu_option_t *opts, int n,
                        const char *title, int cursor)
{
    printf(A_CLEAR);

    printf(A_BOLD A_CYAN
           "╔═════════════════════════════════════════╗\n"
           "║  AVRCP CONSOLE" A_RST A_CYAN "  %-24s" A_BOLD "║\n"
           "╚═════════════════════════════════════════╝\n" A_RST,
           title ? title : "");

    bool connected = g_l2cap_fd >= 0;
    const char *state_color = connected ? A_GREEN : A_RED;
    const char *state_text = connected ? "CONNECTED" : "disconnected";
    const char *state_mark = connected ? "●" : "○";
    const char *target_name = target_name_for_addr();

    if (target_addr_is_zero())
    {
        printf("%s  %s %-12s" A_RST " Target --:--:--:--:--:--  %.18s\n",
               state_color, state_mark, state_text, target_name);
    }
    else
    {
        printf("%s  %s %-12s" A_RST " Target %02x:%02x:%02x:%02x:%02x:%02x  %.18s\n",
               state_color, state_mark, state_text,
               g_target_addr[0], g_target_addr[1], g_target_addr[2],
               g_target_addr[3], g_target_addr[4], g_target_addr[5],
               target_name);
    }
    printf("\n");

    bool top_level = title && strcmp(title, "MAIN") == 0;
    for (int i = 0; i < n; i++)
    {
        const char *row_color = top_level ? main_menu_color(i) : "";
        if (i == cursor)
        {
            printf("%s" A_INV "  %d. %-38s" A_RST "\n",
                   row_color, i + 1, opts[i].label);
        }
        else
        {
            printf("%s  %d. %s" A_RST "\n", row_color, i + 1, opts[i].label);
        }
    }

    bool mesh_ok = mesh_recent();
    printf("\n" A_YELLOW
           "  [W/S=↑↓] [A=back] [D/Enter=select] [1-9=pick] [Q/R=reboot]" A_RST
           "  %sMESH:%s" A_RST "\n  > ",
           mesh_ok ? A_GREEN : A_RED,
           mesh_ok ? "fresh" : "idle");
    fflush(stdout);
}

/* ── loop_options() ─────────────────────────────────────────────────────
 *
 * The heart of the menu engine.  Functionally equivalent to Bruce's
 * loopOptions(): renders, polls for input, dispatches, repeats.
 *
 * Returns -1 when the user backs out (A/ESC or is_back option).
 *
 * No getchar() here.  All input arrives through check(), which reads
 * flags set by input_task().  The 10 ms vTaskDelay in the poll loop
 * prevents watchdog resets and yields the CPU to other tasks.
 * Re-rendering occurs only after a non-zero check() return — no flicker.
 * ───────────────────────────────────────────────────────────────────── */
int loop_options(const menu_option_t *opts, int n, const char *title)
{
    if (!opts || n <= 0)
        return -1;

    int cursor = 0;

    while (1)
    {
        render_menu(opts, n, title, cursor);

        /* ── Poll for input with 10 ms RTOS yield ───────────────────────
         * This is the Bruce loopOptions() wait: the task sleeps between
         * polls so the watchdog never fires and the CPU is shared.
         * Re-rendering only happens after a non-zero check() return.
         * ──────────────────────────────────────────────────────────────── */
        int cmd;
        do
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            cmd = check();
        } while (cmd == 0);

        switch (cmd)
        {
        case 1: /* Up */
            cursor = (cursor - 1 + n) % n;
            break;

        case 2: /* Down */
            cursor = (cursor + 1) % n;
            break;

        case 3: /* Select */
            goto select_current;

        case 4: /* Back / Escape */
            printf(A_CLEAR);
            return -1;

        /* Direct pick 1-9 (cmd 5-13, pick index = cmd - 5) */
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
        {
            int pick = cmd - 5;
            if (pick < n)
            {
                cursor = pick;
                goto select_current;
            }
            /* Out of range: re-render without moving cursor */
            break;
        }

        case 99:             /* Reboot — available from any menu depth */
            action_reboot(); /* never returns */
            break;

        default:
            break;
        }
        /* Cursor moved or out-of-range pick: loop back to render_menu() */
        continue;

    select_current:
        if (opts[cursor].is_back)
        {
            printf(A_CLEAR);
            return -1;
        }

        if (opts[cursor].op)
        {
            opts[cursor].op();
            /* After action returns: loop back, re-render.
             * This is the Bruce loopOptions() pattern.    */
        }
        else
        {
            /* Stub option: show "not yet implemented", wait for any key */
            printf("\n" A_YELLOW
                   "  [!] '%s' — not yet implemented.\n"
                   "  Press any key to continue.\n" A_RST,
                   opts[cursor].label);
            fflush(stdout);
            wait_key();
        }
    }
}

/* ── wait_enter() ───────────────────────────────────────────────────── */
static void wait_enter(const char *msg)
{
    printf("  %s", msg ? msg : "Press any key to continue.");
    fflush(stdout);
    wait_key();
}

/* ════════════════════════════════════════════════════════════════════════
 * Action functions — leaf callbacks wired into menu_option_t arrays.
 *
 * Convention:
 *   - Print a blank line before output so it clears the prompt line.
 *   - End with wait_enter() or a short vTaskDelay so the user sees
 *     output before the menu re-renders.
 *   - Always guard BT operations with g_l2cap_fd checks.
 * ════════════════════════════════════════════════════════════════════════ */

static void action_connect(void)
{
    if (g_l2cap_fd >= 0)
    {
        printf("\n" A_YELLOW "  Already connected. Disconnect first.\n" A_RST);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }

    /* ── Fast path: user just picked a device from the Device List ──────
     * Skip the MAC prompt entirely and connect to g_target_addr now.
     * ─────────────────────────────────────────────────────────────────── */
    if (g_target_just_selected)
    {
        g_target_just_selected = false;

        printf("\n  Connecting to selected target "
               "%02x:%02x:%02x:%02x:%02x:%02x ...\n",
               g_target_addr[0], g_target_addr[1], g_target_addr[2],
               g_target_addr[3], g_target_addr[4], g_target_addr[5]);
        fflush(stdout);

        const char *err_str = NULL;
        esp_err_t ret = do_connect(g_target_addr, &err_str);
        if (ret == ESP_OK)
        {
            printf(A_GREEN "  Connected.\n" A_RST);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        else
        {
            printf(A_RED "  Failed: %s (0x%x)\n" A_RST,
                   err_str ? err_str : "unknown", ret);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        return;
    }

    /* ── Normal path: prompt for MAC ─────────────────────────────────── */
    char buf[32];
    printf("\n  Target MAC [%02x:%02x:%02x:%02x:%02x:%02x],"
           " Enter for default: ",
           g_target_addr[0], g_target_addr[1], g_target_addr[2],
           g_target_addr[3], g_target_addr[4], g_target_addr[5]);
    fflush(stdout);
    read_line(buf, sizeof(buf));

    esp_bd_addr_t addr;
    if (buf[0] != '\0')
    {
        if (sscanf(buf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &addr[0], &addr[1], &addr[2],
                   &addr[3], &addr[4], &addr[5]) != 6)
        {
            printf(A_RED "  Bad MAC format. Use XX:XX:XX:XX:XX:XX\n" A_RST);
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
    }
    else
    {
        if (target_addr_is_zero())
        {
            printf("  No target set. Use Device List to select a device.\n");
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
        memcpy(addr, g_target_addr, sizeof(esp_bd_addr_t));
    }

    printf("  Connecting to %02x:%02x:%02x:%02x:%02x:%02x ...\n",
           addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    fflush(stdout);

    const char *err_str = NULL;
    esp_err_t ret = do_connect(addr, &err_str);
    if (ret == ESP_OK)
    {
        printf(A_GREEN "  Connected.\n" A_RST);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    else
    {
        printf(A_RED "  Failed: %s (0x%x)\n" A_RST,
               err_str ? err_str : "unknown", ret);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void send_volume(uint8_t opcode, const char *name)
{
    if (g_l2cap_fd < 0)
    {
        printf("\n" A_RED "  Not connected.\n" A_RST);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }

    char buf[8];
    printf("\n  Count [15]: ");
    fflush(stdout);
    read_line(buf, sizeof(buf));

    int count = (buf[0] != '\0') ? atoi(buf) : 15;
    if (count <= 0)
    {
        printf("  Nothing to do.\n");
        vTaskDelay(pdMS_TO_TICKS(800));
        return;
    }

    printf("  Sending %d x %s ...\n", count, name);
    fflush(stdout);

    g_abort = false;
    for (int i = 0; i < count; i++)
    {
        if (g_abort || g_l2cap_fd < 0)
        {
            printf(A_YELLOW "  Interrupted at press %d.\n" A_RST, i + 1);
            break;
        }
        send_avrcp_passthrough(g_l2cap_fd, s_tl, opcode, 0x00); /* PRESS   */
        vTaskDelay(pdMS_TO_TICKS(200));
        send_avrcp_passthrough(g_l2cap_fd, s_tl, opcode, 0x80); /* RELEASE */
        vTaskDelay(pdMS_TO_TICKS(500));
        s_tl = (s_tl + 1) & 0x0F;
    }

    if (!g_abort && g_l2cap_fd >= 0)
    {
        printf(A_GREEN "  Done.\n" A_RST);
    }
    vTaskDelay(pdMS_TO_TICKS(600));
}

static void action_vol_up(void) { send_volume(0x41, "Volume Up"); }
static void action_vol_down(void) { send_volume(0x42, "Volume Down"); }

static void action_disconnect(void)
{
    if (g_l2cap_fd < 0)
    {
        printf("\n  Not connected.\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }
    printf("\n  Disconnecting ...\n");
    fflush(stdout);
    do_disconnect();
    printf(A_GREEN "  Disconnected.\n" A_RST);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

static void action_status(void)
{
    printf("\n");
    printf("  +--------------------------------------+\n");
    printf("  | Connection : %-24s|\n",
           g_l2cap_fd >= 0 ? "CONNECTED" : "disconnected");
    printf("  | Target MAC : %02x:%02x:%02x:%02x:%02x:%02x       |\n",
           g_target_addr[0], g_target_addr[1], g_target_addr[2],
           g_target_addr[3], g_target_addr[4], g_target_addr[5]);
    printf("  | l2cap_fd   : %-24d|\n", g_l2cap_fd);
    printf("  | TL counter : %-24u|\n", (unsigned)s_tl);
    printf("  | Devices    : %-24u|\n", (unsigned)g_remote_device_count);
    printf("  +--------------------------------------+\n");
    wait_enter(NULL);
}

static void action_reboot(void)
{
    printf("\n" A_YELLOW "  Rebooting in 1 s ...\n" A_RST);
    fflush(stdout);
    do_disconnect();
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

/* ── remote_device_update() ─────────────────────────────────────────────
 * Called from espnow_receive_task() in main.c whenever a CMD_SEND_DEVICE
 * packet arrives from the scanner ESP32.
 * ───────────────────────────────────────────────────────────────────── */
void remote_device_update(device_info_t *d)
{
    /* Search for existing MAC */
    for (int i = 0; i < MAX_REMOTE_DEVICES; i++)
    {
        if (g_remote_devices[i].in_use &&
            memcmp(g_remote_devices[i].bda, d->bda, 6) == 0)
        {
            /* Update existing entry */
            g_remote_devices[i].rssi = d->rssi;
            g_remote_devices[i].cod = d->cod;
            g_remote_devices[i].type = d->type;
            g_remote_devices[i].tx_power = d->tx_power;
            g_remote_devices[i].company_id = d->company_id;
            strncpy(g_remote_devices[i].vendor, d->vendor,
                    sizeof(g_remote_devices[i].vendor) - 1);
            g_remote_devices[i].vendor[sizeof(g_remote_devices[i].vendor) - 1] = '\0';
            g_remote_devices[i].last_seen_ms = get_now_ms();
            if (d->name[0])
            {
                strncpy(g_remote_devices[i].name, d->name, 31);
                g_remote_devices[i].name[31] = '\0';
            }
            return;
        }
    }
    /* Insert new entry in first free slot */
    for (int i = 0; i < MAX_REMOTE_DEVICES; i++)
    {
        if (!g_remote_devices[i].in_use)
        {
            memset(&g_remote_devices[i], 0, sizeof(g_remote_devices[i]));
            memcpy(g_remote_devices[i].bda, d->bda, 6);
            g_remote_devices[i].in_use = true;
            g_remote_devices[i].rssi = d->rssi;
            g_remote_devices[i].cod = d->cod;
            g_remote_devices[i].type = d->type;
            g_remote_devices[i].tx_power = d->tx_power;
            g_remote_devices[i].company_id = d->company_id;
            strncpy(g_remote_devices[i].vendor, d->vendor,
                    sizeof(g_remote_devices[i].vendor) - 1);
            g_remote_devices[i].vendor[sizeof(g_remote_devices[i].vendor) - 1] = '\0';
            g_remote_devices[i].last_seen_ms = get_now_ms();
            if (d->name[0])
            {
                strncpy(g_remote_devices[i].name, d->name, 31);
                g_remote_devices[i].name[31] = '\0';
            }
            g_remote_device_count++;
            return;
        }
    }
    /* Table full — silently drop */
}

/* ── action_select_device() ─────────────────────────────────────────────
 * Copies the MAC of the device at g_selected_device_index into
 * g_target_addr and prints a brief confirmation.
 * ───────────────────────────────────────────────────────────────────── */
static void action_select_device(void)
{
    int idx = g_selected_device_index;
    if (idx < 0 || idx >= MAX_REMOTE_DEVICES || !g_remote_devices[idx].in_use)
    {
        printf("\n" A_RED "  Invalid device selection.\n" A_RST);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }

    memcpy(g_target_addr, g_remote_devices[idx].bda, sizeof(esp_bd_addr_t));
    g_target_just_selected = true;

    printf("\n" A_GREEN "  Target set: %02x:%02x:%02x:%02x:%02x:%02x  %s\n" A_RST,
           g_target_addr[0], g_target_addr[1], g_target_addr[2],
           g_target_addr[3], g_target_addr[4], g_target_addr[5],
           g_remote_devices[idx].name[0] ? g_remote_devices[idx].name : "(unknown)");
    vTaskDelay(pdMS_TO_TICKS(1200));
}

/* ── submenu_devices() ──────────────────────────────────────────────────
 * Displays all devices received from the scanner ESP32 via ESP-NOW and
 * lets the user pick one as the attack target.
 * ───────────────────────────────────────────────────────────────────── */
static void submenu_devices(void)
{
    /* Count active entries */
    int active = 0;
    for (int i = 0; i < MAX_REMOTE_DEVICES; i++)
    {
        if (g_remote_devices[i].in_use)
        {
            active++;
        }
    }

    if (active == 0)
    {
        printf(A_CLEAR);
        printf("\n" A_YELLOW "  No devices received yet.\n" A_RST);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1800));
        return;
    }

    /* Allocate index_map and menu entries (+1 for Back) */
    int *index_map = calloc(active, sizeof(int));
    menu_option_t *opts = calloc(active + 1, sizeof(menu_option_t));
    /* Each label: "XX:XX:XX:XX:XX:XX  Cls  -99  name..." */
    char (*labels)[56] = calloc(active, sizeof(*labels));

    if (!index_map || !opts || !labels)
    {
        free(index_map);
        free(opts);
        free(labels);
        printf("\n" A_RED "  Out of memory.\n" A_RST);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }

    /* Populate entries */
    int slot = 0;
    char bda_str[18];
    for (int i = 0; i < MAX_REMOTE_DEVICES && slot < active; i++)
    {
        if (!g_remote_devices[i].in_use)
            continue;

        bda2str(g_remote_devices[i].bda, bda_str, sizeof(bda_str));
        const char *type_str = (g_remote_devices[i].type == 1) ? "BLE" : "Cls";
        const char *name_str = g_remote_devices[i].name[0]
                                   ? g_remote_devices[i].name
                                   : "(unknown)";

        snprintf(labels[slot], sizeof(*labels),
                 "%-17s %-3s %4d  %.18s",
                 bda_str, type_str,
                 (int)g_remote_devices[i].rssi,
                 name_str);

        index_map[slot] = i;
        opts[slot].label = labels[slot];
        opts[slot].op = NULL;
        opts[slot].is_back = false;
        slot++;
    }

    /* Back entry */
    opts[active].label = "Back";
    opts[active].op = NULL;
    opts[active].is_back = true;

    int n = active + 1;
    int cursor = 0;

    /* Navigation loop */
    while (1)
    {
        render_menu(opts, n, "DEVICE LIST", cursor);

        int cmd;
        do
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            cmd = check();
        } while (cmd == 0);

        switch (cmd)
        {
        case 1: /* Up */
            cursor = (cursor - 1 + n) % n;
            break;

        case 2: /* Down */
            cursor = (cursor + 1) % n;
            break;

        case 3: /* Select */
            if (opts[cursor].is_back)
            {
                printf(A_CLEAR);
                goto devices_done;
            }
            g_selected_device_index = index_map[cursor];
            action_select_device();
            goto devices_done;

        case 4: /* Back */
            printf(A_CLEAR);
            goto devices_done;

        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
        {
            int pick = cmd - 5;
            if (pick < n)
            {
                cursor = pick;
                if (opts[cursor].is_back)
                {
                    printf(A_CLEAR);
                    goto devices_done;
                }
                g_selected_device_index = index_map[cursor];
                action_select_device();
                goto devices_done;
            }
            break;
        }

        case 99: /* Reboot */
            free(index_map);
            free(opts);
            free(labels);
            action_reboot(); /* never returns */
            break;

        default:
            break;
        }
    }

devices_done:
    free(index_map);
    free(opts);
    free(labels);
}

/* ════════════════════════════════════════════════════════════════════════
 * Submenu builders — each calls loop_options() with a local opts[] array.
 * ════════════════════════════════════════════════════════════════════════ */

static void submenu_avrcp(void)
{
    static const menu_option_t opts[] = {
        MENU_OPT("Connect", action_connect),
        MENU_OPT("Volume Up", action_vol_up),
        MENU_OPT("Volume Down", action_vol_down),
        MENU_OPT("Disconnect", action_disconnect),
        MENU_BACK(),
    };
    loop_options(opts, sizeof(opts) / sizeof(opts[0]), "AVRCP");
}

static void submenu_jlspp(void)
{
    static const menu_option_t opts[] = {
        MENU_STUB("Opcode Scan ch1  [future]"),
        MENU_STUB("Reset PRNG 0x47  [future]"),
        MENU_STUB("Ch10 Auth Probe  [future]"),
        MENU_STUB("Timing Analysis  [future]"),
        MENU_BACK(),
    };
    loop_options(opts, sizeof(opts) / sizeof(opts[0]), "JL-SPP");
}

static void submenu_bluetooth(void)
{
    static const menu_option_t opts[] = {
        MENU_OPT("AVRCP", submenu_avrcp),
        MENU_OPT("JL-SPP", submenu_jlspp),
        MENU_OPT("Device List", submenu_devices),
        MENU_BACK(),
    };
    loop_options(opts, sizeof(opts) / sizeof(opts[0]), "BLUETOOTH");
}

static void submenu_wifi(void)
{
    static const menu_option_t opts[] = {
        MENU_STUB("Wi-Fi scanning [future]"),
        MENU_BACK(),
    };
    loop_options(opts, sizeof(opts) / sizeof(opts[0]), "WI-FI");
}

static void submenu_rfid(void)
{
    static const menu_option_t opts[] = {
        MENU_STUB("RFID [future]"),
        MENU_BACK(),
    };
    loop_options(opts, sizeof(opts) / sizeof(opts[0]), "RFID");
}

/* ════════════════════════════════════════════════════════════════════════
 * menu_run_main() — top-level entry point.
 *
 * Called once from app_main() after BT init and input_task spawn.
 * Never returns.  A=back at top level re-enters the main menu.
 * Q/R=reboot is handled inside loop_options() at any depth.
 * ════════════════════════════════════════════════════════════════════════ */
void menu_run_main(void)
{
    static const menu_option_t main_opts[] = {
        MENU_OPT("Bluetooth", submenu_bluetooth),
        MENU_OPT("Wi-Fi (soon)", submenu_wifi),
        MENU_OPT("RFID (soon)", submenu_rfid),
        MENU_OPT("Status", action_status),
        MENU_OPT("Reboot", action_reboot),
    };
    const int n = sizeof(main_opts) / sizeof(main_opts[0]);

    for (;;)
    {
        loop_options(main_opts, n, "MAIN");
    }
}

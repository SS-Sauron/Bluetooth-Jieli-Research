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
#define A_CLEAR   "\033[2J\033[H"
#define A_BOLD    "\033[1m"
#define A_RST     "\033[0m"
#define A_INV     "\033[7m"
#define A_CYAN    "\033[36m"
#define A_YELLOW  "\033[33m"
#define A_GREEN   "\033[32m"
#define A_RED     "\033[31m"

/* ── Persistent AVRCP transaction label ─────────────────────────────── */
static uint8_t s_tl = 0;

/* ── Global input flags ─────────────────────────────────────────────────
 * Written exclusively by input_task() (main.c).
 * Read and cleared exclusively by check() and wait_key() (this file).
 * ───────────────────────────────────────────────────────────────────── */
volatile bool    g_up_press     = false;
volatile bool    g_down_press   = false;
volatile bool    g_sel_press    = false;
volatile bool    g_esc_press    = false;
volatile bool    g_reboot_press = false;
volatile bool    g_any_press    = false;
volatile uint8_t g_direct_pick  = 0;

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
    if (g_reboot_press) {
        g_reboot_press = false;   /* individual flag first */
        g_any_press    = false;   /* sentinel last         */
        return 99;
    }

    int ret = 0;

    if (g_any_press) {
        if (g_up_press) {
            g_up_press  = false;
            ret = 1;
        } else if (g_down_press) {
            g_down_press = false;
            ret = 2;
        } else if (g_sel_press) {
            g_sel_press  = false;
            ret = 3;
        } else if (g_esc_press) {
            g_esc_press  = false;
            ret = 4;
        }
        g_any_press = false;   /* sentinel cleared last — always */
    }

    /* Direct pick overrides nav return if both arrive simultaneously */
    if (g_direct_pick != 0) {
        ret = 4 + (int)g_direct_pick;   /* 1-9 maps to 5-13 */
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
    while (!g_any_press && g_direct_pick == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    /* Clear everything — caller only needed "any key", not which one */
    g_up_press = g_down_press = g_sel_press =
    g_esc_press = g_reboot_press = false;
    g_any_press   = false;
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
           "┌─────────────────────────────────────────┐\n"
           "│  AVRCP CONSOLE" A_RST A_CYAN "  %-24s" A_BOLD "│\n"
           "└─────────────────────────────────────────┘\n" A_RST,
           title ? title : "");

    if (g_l2cap_fd >= 0) {
        printf(A_GREEN "  ● CONNECTED  "
               "%02x:%02x:%02x:%02x:%02x:%02x\n" A_RST,
               g_target_addr[0], g_target_addr[1], g_target_addr[2],
               g_target_addr[3], g_target_addr[4], g_target_addr[5]);
    } else {
        printf(A_RED "  ○ disconnected\n" A_RST);
    }
    printf("\n");

    for (int i = 0; i < n; i++) {
        if (i == cursor) {
            printf(A_INV "  %d. %-38s" A_RST "\n", i + 1, opts[i].label);
        } else {
            printf("  %d. %s\n", i + 1, opts[i].label);
        }
    }

    printf("\n" A_YELLOW
           "  [W/S=↑↓] [A=back] [D/Enter=select] [1-9=pick] [Q/R=reboot]"
           A_RST "\n  > ");
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
    if (!opts || n <= 0) return -1;

    int cursor = 0;

    while (1) {
        render_menu(opts, n, title, cursor);

        /* ── Poll for input with 10 ms RTOS yield ───────────────────────
         * This is the Bruce loopOptions() wait: the task sleeps between
         * polls so the watchdog never fires and the CPU is shared.
         * Re-rendering only happens after a non-zero check() return.
         * ──────────────────────────────────────────────────────────────── */
        int cmd;
        do {
            vTaskDelay(pdMS_TO_TICKS(10));
            cmd = check();
        } while (cmd == 0);

        switch (cmd) {
            case 1:   /* Up */
                cursor = (cursor - 1 + n) % n;
                break;

            case 2:   /* Down */
                cursor = (cursor + 1) % n;
                break;

            case 3:   /* Select */
                goto select_current;

            case 4:   /* Back / Escape */
                printf(A_CLEAR);
                return -1;

            /* Direct pick 1-9 (cmd 5-13, pick index = cmd - 5) */
            case 5: case 6: case 7: case 8: case 9:
            case 10: case 11: case 12: case 13:
            {
                int pick = cmd - 5;
                if (pick < n) {
                    cursor = pick;
                    goto select_current;
                }
                /* Out of range: re-render without moving cursor */
                break;
            }

            case 99:  /* Reboot — available from any menu depth */
                action_reboot();   /* never returns */
                break;

            default:
                break;
        }
        /* Cursor moved or out-of-range pick: loop back to render_menu() */
        continue;

    select_current:
        if (opts[cursor].is_back) {
            printf(A_CLEAR);
            return -1;
        }

        if (opts[cursor].op) {
            opts[cursor].op();
            /* After action returns: loop back, re-render.
             * This is the Bruce loopOptions() pattern.    */
        } else {
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
    if (g_l2cap_fd >= 0) {
        printf("\n" A_YELLOW "  Already connected. Disconnect first.\n" A_RST);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }

    char buf[32];
    printf("\n  Target MAC [%02x:%02x:%02x:%02x:%02x:%02x],"
           " Enter for default: ",
           g_target_addr[0], g_target_addr[1], g_target_addr[2],
           g_target_addr[3], g_target_addr[4], g_target_addr[5]);
    fflush(stdout);
    read_line(buf, sizeof(buf));

    esp_bd_addr_t addr;
    if (buf[0] != '\0') {
        if (sscanf(buf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &addr[0], &addr[1], &addr[2],
                   &addr[3], &addr[4], &addr[5]) != 6) {
            printf(A_RED "  Bad MAC format. Use XX:XX:XX:XX:XX:XX\n" A_RST);
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
    } else {
        memcpy(addr, g_target_addr, sizeof(esp_bd_addr_t));
    }

    printf("  Connecting to %02x:%02x:%02x:%02x:%02x:%02x ...\n",
           addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    fflush(stdout);

    const char *err_str = NULL;
    esp_err_t ret = do_connect(addr, &err_str);
    if (ret == ESP_OK) {
        printf(A_GREEN "  Connected.\n" A_RST);
        vTaskDelay(pdMS_TO_TICKS(1000));
    } else {
        printf(A_RED "  Failed: %s (0x%x)\n" A_RST,
               err_str ? err_str : "unknown", ret);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void send_volume(uint8_t opcode, const char *name)
{
    if (g_l2cap_fd < 0) {
        printf("\n" A_RED "  Not connected.\n" A_RST);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }

    char buf[8];
    printf("\n  Count [15]: ");
    fflush(stdout);
    read_line(buf, sizeof(buf));

    int count = (buf[0] != '\0') ? atoi(buf) : 15;
    if (count <= 0) {
        printf("  Nothing to do.\n");
        vTaskDelay(pdMS_TO_TICKS(800));
        return;
    }

    printf("  Sending %d x %s ...\n", count, name);
    fflush(stdout);

    g_abort = false;
    for (int i = 0; i < count; i++) {
        if (g_abort || g_l2cap_fd < 0) {
            printf(A_YELLOW "  Interrupted at press %d.\n" A_RST, i + 1);
            break;
        }
        send_avrcp_passthrough(g_l2cap_fd, s_tl, opcode, 0x00); /* PRESS   */
        vTaskDelay(pdMS_TO_TICKS(200));
        send_avrcp_passthrough(g_l2cap_fd, s_tl, opcode, 0x80); /* RELEASE */
        vTaskDelay(pdMS_TO_TICKS(500));
        s_tl = (s_tl + 1) & 0x0F;
    }

    if (!g_abort && g_l2cap_fd >= 0) {
        printf(A_GREEN "  Done.\n" A_RST);
    }
    vTaskDelay(pdMS_TO_TICKS(600));
}

static void action_vol_up(void)   { send_volume(0x41, "Volume Up");   }
static void action_vol_down(void) { send_volume(0x42, "Volume Down"); }

static void action_disconnect(void)
{
    if (g_l2cap_fd < 0) {
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

/* ════════════════════════════════════════════════════════════════════════
 * Submenu builders — each calls loop_options() with a local opts[] array.
 * ════════════════════════════════════════════════════════════════════════ */

static void submenu_avrcp(void)
{
    static const menu_option_t opts[] = {
        MENU_OPT("Connect",      action_connect),
        MENU_OPT("Volume Up",    action_vol_up),
        MENU_OPT("Volume Down",  action_vol_down),
        MENU_OPT("Disconnect",   action_disconnect),
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
        MENU_OPT("AVRCP",    submenu_avrcp),
        MENU_OPT("JL-SPP",   submenu_jlspp),
        MENU_OPT("Status",   action_status),
        MENU_OPT("Reboot",   action_reboot),
    };
    const int n = sizeof(main_opts) / sizeof(main_opts[0]);

    for (;;) {
        loop_options(main_opts, n, "MAIN");
    }
}

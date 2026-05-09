/*
 * menu.c — Bruce-style loopOptions menu for the AVRCP serial console.
 *
 * Implements:
 *   loop_options()    — core navigation engine (serial/ANSI)
 *   action_*()        — leaf action callbacks
 *   submenu_*()       — sub-menu wrappers
 *   menu_run_main()   — top-level entry point
 *
 * Adding a new action:
 *   1. Write a static void action_foo(void) function here.
 *   2. Add MENU_OPT("Foo", action_foo) to the relevant opts[] array.
 *   Done.  No handler registration, no esp_console glue needed.
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
#include "driver/uart.h"   /* uart_read_bytes() for single-key raw input */

#define TAG "MENU"

/* ── ANSI escape helpers ────────────────────────────────────────────────
 * All standard VT100; works in idf.py monitor, PuTTY, screen, minicom.
 * If your terminal doesn't support ANSI, the menus still work — they
 * just render with extra escape bytes visible.  Set MENU_NO_ANSI=1 in
 * sdkconfig to strip them (future work).
 * ───────────────────────────────────────────────────────────────────── */
#define A_CLEAR   "\033[2J\033[H"    /* clear screen, cursor home        */
#define A_BOLD    "\033[1m"
#define A_RST     "\033[0m"
#define A_INV     "\033[7m"          /* reverse video (highlight cursor)  */
#define A_CYAN    "\033[36m"
#define A_YELLOW  "\033[33m"
#define A_GREEN   "\033[32m"
#define A_RED     "\033[31m"

/* ── Persistent AVRCP transaction label ────────────────────────────────
 * Persists across action calls so the label counter increments correctly
 * even when the user runs vol-up and vol-down in alternation.
 * ───────────────────────────────────────────────────────────────────── */
static uint8_t s_tl = 0;

/* ── Raw single-key input helpers ───────────────────────────────────────
 * These bypass the VFS/stdio read-line layer so navigation is instant —
 * no Enter required.  Both rely on the UART driver ring buffer installed
 * by esp_console_init() in app_main().
 *
 * getch()         — blocks indefinitely until one byte arrives.
 * getch_timeout() — returns -1 if no byte arrives within timeout_ms.
 * ───────────────────────────────────────────────────────────────────── */
static int getch(void)
{
    uint8_t ch;
    if (uart_read_bytes(UART_NUM_0, &ch, 1, portMAX_DELAY) > 0)
        return (int)ch;
    return -1;
}

static int getch_timeout(int timeout_ms)
{
    uint8_t ch;
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    if (ticks == 0) ticks = 1;          /* minimum 1 tick */
    if (uart_read_bytes(UART_NUM_0, &ch, 1, ticks) > 0)
        return (int)ch;
    return -1;
}

/* Forward declaration — loop_options() calls action_reboot() for Q/R */
static void action_reboot(void);

/* ── loop_options() ────────────────────────────────────────────────────
 * The heart of the menu engine.  Functionally equivalent to Bruce's
 * loopOptions(): renders, waits for input, dispatches, repeats.
 *
 * Returns -1 when the user backs out (A key or is_back option chosen).
 * The caller (submenu wrapper or menu_run_main) handles the -1.
 * ───────────────────────────────────────────────────────────────────── */

static void render_menu(const menu_option_t *opts, int n,
                         const char *title, int cursor)
{
    printf(A_CLEAR);

    /* ── Header bar ── */
    printf(A_BOLD A_CYAN
           "┌─────────────────────────────────────────┐\n"
           "│  AVRCP CONSOLE" A_RST A_CYAN "  %-24s" A_BOLD "│\n"
           "└─────────────────────────────────────────┘\n" A_RST,
           title ? title : "");

    /* ── Connection status badge ── */
    if (g_l2cap_fd >= 0) {
        printf(A_GREEN "  ● CONNECTED  "
               "%02x:%02x:%02x:%02x:%02x:%02x\n" A_RST,
               g_target_addr[0], g_target_addr[1], g_target_addr[2],
               g_target_addr[3], g_target_addr[4], g_target_addr[5]);
    } else {
        printf(A_RED "  ○ disconnected\n" A_RST);
    }
    printf("\n");

    /* ── Option list ── */
    for (int i = 0; i < n; i++) {
        bool selected = (i == cursor);
        if (selected) {
            printf(A_INV "  %d. %-38s" A_RST "\n", i + 1, opts[i].label);
        } else {
            printf("  %d. %s\n", i + 1, opts[i].label);
        }
    }

    /* ── Nav hint ── */
    printf("\n" A_YELLOW
           "  [W/S=↑↓] [A=back] [D/Enter=select] [1-9=pick] [Q/R=reboot]"
           A_RST "\n  > ");
    fflush(stdout);
}

int loop_options(const menu_option_t *opts, int n, const char *title)
{
    if (!opts || n <= 0) return -1;

    int cursor = 0;

    while (1) {
        render_menu(opts, n, title, cursor);

        /* ── Raw single-key input — no Enter required ───────────────────
         * After reading one byte:
         *   - ESC (0x1B) → try to read a 2-byte ANSI CSI sequence within
         *     50 ms; map arrows to WASD equivalents.  Bare ESC = back.
         *   - Everything else mapped directly.
         * ──────────────────────────────────────────────────────────────── */
        int raw = getch();
        char ch  = 0;

        if (raw == 0x1B) {
            /* Potential arrow-key sequence: ESC [ A/B/C/D */
            int b1 = getch_timeout(50);
            if (b1 == '[') {
                int b2 = getch_timeout(50);
                if      (b2 == 'A') ch = 'w';   /* ↑ → W */
                else if (b2 == 'B') ch = 's';   /* ↓ → S */
                else if (b2 == 'C') ch = '\r';  /* → → select */
                else if (b2 == 'D') ch = 'a';   /* ← → A (back) */
            }
            if (ch == 0) ch = 'a';              /* bare ESC = back */
        } else {
            ch = (char)raw;
        }

        /* ── Direct pick by digit (1-9) ── */
        if (ch >= '1' && ch <= '9') {
            int pick = (int)(ch - '1');
            if (pick < n) {
                cursor = pick;
                goto select_current;
            }
            continue;   /* out of range: re-render */
        }

        /* ── Navigation ── */
        if (ch == 'w' || ch == 'W') {
            cursor = (cursor - 1 + n) % n;
            continue;
        }
        if (ch == 's' || ch == 'S') {
            cursor = (cursor + 1) % n;
            continue;
        }

        /* ── Back ── */
        if (ch == 'a' || ch == 'A') {
            printf(A_CLEAR);
            return -1;
        }

        /* ── Select (D or Enter) ── */
        if (ch == 'd' || ch == 'D' || ch == '\r' || ch == '\n') {
            goto select_current;
        }

        /* ── Reboot (Q or R) — available from any menu depth ── */
        if (ch == 'q' || ch == 'Q' || ch == 'r' || ch == 'R') {
            action_reboot();   /* never returns */
            continue;          /* unreachable; silences compiler */
        }

        /* Unknown key: re-render */
        continue;

    select_current:
        if (opts[cursor].is_back) {
            printf(A_CLEAR);
            return -1;
        }

        if (opts[cursor].op) {
            opts[cursor].op();
            /* After action returns, fall through to top of while(1):
             * re-render the menu.  This is the Bruce loopOptions pattern. */
        } else {
            /* Stub option — "not yet implemented" notice, any key to dismiss */
            printf("\n" A_YELLOW
                   "  [!] '%s' — not yet implemented.\n"
                   "  Press any key to continue.\n" A_RST,
                   opts[cursor].label);
            fflush(stdout);
            getch();
        }
    }
}

/* ── Shared wait helper ─────────────────────────────────────────────── */
static void wait_enter(const char *msg)
{
    printf("  %s", msg ? msg : "Press any key to continue.");
    fflush(stdout);
    getch();   /* single keypress — no Enter needed */
}

/* ════════════════════════════════════════════════════════════════════════
 * Action functions — leaf callbacks wired into menu_option_t arrays.
 *
 * Convention:
 *   - Print a blank line before output so it's below the prompt.
 *   - End with wait_enter() or a short vTaskDelay so the user sees output
 *     before the menu re-renders.
 *   - Always guard BT operations with g_l2cap_fd checks.
 * ════════════════════════════════════════════════════════════════════════ */

/* ── Connect ─────────────────────────────────────────────────────────── */
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

/* ── Volume send helper ──────────────────────────────────────────────── */
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

    printf("  Sending %d × %s ...\n", count, name);
    fflush(stdout);

    g_abort = false;
    for (int i = 0; i < count; i++) {
        /* Guard: exit early if connection dropped between presses */
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

/* ── Disconnect ──────────────────────────────────────────────────────── */
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

/* ── Status ──────────────────────────────────────────────────────────── */
static void action_status(void)
{
    printf("\n");
    printf("  ┌──────────────────────────────────────┐\n");
    printf("  │ Connection : %-26s│\n",
           g_l2cap_fd >= 0 ? "CONNECTED" : "disconnected");
    printf("  │ Target MAC : %02x:%02x:%02x:%02x:%02x:%02x           │\n",
           g_target_addr[0], g_target_addr[1], g_target_addr[2],
           g_target_addr[3], g_target_addr[4], g_target_addr[5]);
    printf("  │ l2cap_fd   : %-26d│\n", g_l2cap_fd);
    printf("  │ TL counter : %-26u│\n", (unsigned)s_tl);
    printf("  └──────────────────────────────────────┘\n");
    wait_enter(NULL);
}

/* ── Reboot ──────────────────────────────────────────────────────────── */
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
 * Adding a new submenu: write action functions above, then add a new
 * static void submenu_foo(void) block and wire it into main_opts[] below.
 * ════════════════════════════════════════════════════════════════════════ */

/* ── AVRCP submenu ───────────────────────────────────────────────────── */
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

/* ── JL-SPP submenu (stubs — fills out in parallel with FUTURE_WORK.md) */
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
 * Called once from app_main() after BT init.  Never returns.
 * A=back at top level just re-enters; Q/R=reboot works at any depth.
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

    /* A=back at the top level just re-enters — nowhere higher to go.
     * Q/R=reboot is handled inside loop_options() for any depth.       */
    for (;;) {
        loop_options(main_opts, n, "MAIN");
    }
}

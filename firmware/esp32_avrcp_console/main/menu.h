/*
 * menu.h — Bruce-style loopOptions menu engine for the AVRCP console.
 *
 * Adapts Bruce firmware's Option struct + loopOptions() pattern to a
 * serial-only (UART/ANSI) ESP-IDF environment.  No TFT required.
 *
 * Navigation (any VT100 terminal, including idf.py monitor):
 *   W / ↑          → previous item
 *   S / ↓          → next item
 *   A / ← / Esc    → back
 *   D / → / Enter  → select current
 *   1-9            → pick by number
 *   Q / R          → reboot
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_bt_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Shared state (defined in main.c) ──────────────────────────────────
 * These were made non-static so menu.c can access them directly.
 * ───────────────────────────────────────────────────────────────────── */
extern SemaphoreHandle_t g_l2cap_sem;
extern SemaphoreHandle_t g_acl_disc_sem;
extern int               g_l2cap_fd;
extern esp_bd_addr_t     g_target_addr;
extern volatile uint8_t  g_avrcp_opcode;
extern volatile int      g_repeats;
extern volatile bool     g_abort;

/* ── Shared functions (defined in main.c) ───────────────────────────── */
bool      read_line(char *buf, size_t len);
void      send_avrcp_passthrough(int fd, uint8_t tl,
                                  uint8_t op_data, uint8_t state);
esp_err_t do_connect(esp_bd_addr_t addr, const char **err_str);
void      do_disconnect(void);

/* ── Option struct (Bruce-inspired, C version) ──────────────────────── */
typedef void (*menu_op_t)(void);

typedef struct {
    const char *label;   /* Text shown in the menu list (max ~40 chars) */
    menu_op_t   op;      /* Called on select; NULL = unimplemented stub  */
    bool        is_back; /* true → exit loop_options(), return -1        */
} menu_option_t;

/* Convenience macros — mirror Bruce's Option construction patterns */
#define MENU_OPT(lbl, fn)  { (lbl), (fn),  false }
#define MENU_STUB(lbl)     { (lbl), NULL,   false }   /* placeholder     */
#define MENU_BACK()        { "Back", NULL,  true  }

/*
 * loop_options() — the serial clone of Bruce's loopOptions().
 *
 * Displays `title` + a numbered list of `n` options.  Blocks on input.
 * After each selection, calls opt.op() (if non-NULL) then re-renders.
 * Exits — returning -1 — only when user presses A/ESC or chooses is_back.
 *
 * Identical control flow to Bruce: render → wait → dispatch → render.
 */
int loop_options(const menu_option_t *opts, int n, const char *title);

/* Top-level entry point — called from app_main() after BT init */
void menu_run_main(void);

#ifdef __cplusplus
}
#endif

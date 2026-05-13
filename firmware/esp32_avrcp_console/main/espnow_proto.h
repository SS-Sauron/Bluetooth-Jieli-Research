#pragma once

#include <stdint.h>

/*
 * espnow_proto.h — ESP-NOW wire protocol for scanner ↔ attack coordination.
 *
 * Scanner ESP32  (USB1):  78:1c:3c:a8:dc:72
 * Attack  ESP32  (USB0):  78:1c:3c:a5:a8:d2
 *
 * Message flow:
 *   Scanner  →  Attack   CMD_SEND_DEVICE    a Bluetooth device was found
 *   Any peer →  Attack   CMD_SET_TARGET     set the target MAC for the next attack
 *   Any peer →  Attack   CMD_LAUNCH_ATTACK  trigger the current menu action
 */

/* ── Peer MAC addresses ─────────────────────────────────────────────────── */

#define PEER_SCANNER_MAC {0x78, 0x1c, 0x3c, 0xa8, 0xdc, 0x72}
#define PEER_ATTACK_MAC {0x78, 0x1c, 0x3c, 0xa5, 0xa8, 0xd0}

/* ── Command IDs ────────────────────────────────────────────────────────── */

typedef enum
{
    CMD_SEND_DEVICE = 0x01,   /* scanner → attack : a device was found       */
    CMD_SET_TARGET = 0x02,    /* any peer → attack: set target MAC            */
    CMD_LAUNCH_ATTACK = 0x03, /* any peer → attack: trigger current menu action */
} espnow_cmd_id_t;

/* ── Device info payload (CMD_SEND_DEVICE) ──────────────────────────────── */

typedef struct
{
    uint8_t bda[6]; /* Bluetooth MAC address of the discovered device     */
    char name[32];  /* Device name — max 31 chars + null terminator       */
    int8_t rssi;    /* Signal strength in dBm                             */
    uint32_t cod;   /* Class of Device (0 for BLE devices)                */
    uint8_t type;   /* 0 = Classic BR/EDR,  1 = BLE                      */
    int8_t tx_power;     /* TX Power in dBm, or 127 if unknown             */
    uint16_t company_id; /* Bluetooth SIG Company ID, 0 if unknown         */
    char vendor[32];     /* OUI vendor name, "(unknown)" until filled      */
} device_info_t;

/* ── Top-level command packet ───────────────────────────────────────────── */

typedef struct
{
    uint8_t cmd_id; /* One of espnow_cmd_id_t                     */
    union
    {
        device_info_t device; /* CMD_SEND_DEVICE  payload                   */
        uint8_t mac[6];       /* CMD_SET_TARGET   payload (target BD_ADDR)  */
    } payload;
} command_t;

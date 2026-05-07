# AVRCP Packet Structure Technical Reference

**Audience:** Security researchers, embedded developers, Bluetooth stack maintainers.  
**Sources:** AVRCP 1.6.2 Specification (§6.4), AVCTP 1.0 Specification, BlueZ test‑suite captures, ESP32 raw‑L2CAP experiments.

## 1. Protocol Stack Position

AVRCP sits at the application layer, carried by two transport layers:
┌──────────────────────────────┐
│ AVRCP (1.6.2) │ ← Application: command/response semantics
├──────────────────────────────┤
│ AVCTP (1.0) │ ← Transport: transaction labels, fragmentation
├──────────────────────────────┤
│ L2CAP (PSM 23) │ ← Logical link: multiplexing
├──────────────────────────────┤
│ Baseband / ACL │ ← Physical: BD_ADDR, hopping
└──────────────────────────────┘


## 2. AVCTP Header (3 bytes)

Every AVRCP frame begins with a 3‑byte AVCTP header.  The format is defined by the AVCTP 1.0 Specification:

| Byte | Bits | Field | Description |
| :--- | :--- | :--- | :--- |
| 0 | 7‑4 | **Transaction Label** | 4‑bit identifier; increments per command (0x0–0xF).  Echoed in the response by the target. |
| 0 | 3‑2 | **Packet Type** | `00` = single (non‑fragmented) packet.  `01` = start fragment, `10` = continue, `11` = end. |
| 0 | 1 | **C/R** | `0` = Command frame, `1` = Response frame. |
| 0 | 0 | **IPID** | Invalid Profile Identifier.  `0` = PID is valid; `1` in a response means "unknown PID received." |
| 1‑2 | — | **Profile ID** | 16‑bit big‑endian UUID: `0x110E` = AVRCP. |

**Example:** For a `PASSTHROUGH` command with transaction label 0x2:
0x20 0x11 0x0E

→ Transaction label = 2, packet type = 0, C/R = 0, IPID = 0, Profile = AVRCP.

## 3. AV/C PASSTHROUGH Command Frame (5 bytes)

Following the AVCTP header, the AV/C command frame for `PASSTHROUGH` (opcode `0x7C`) carries the actual media‑control key code.

| Offset | Field | Value(s) | Spec Reference |
| :--- | :--- | :--- | :--- |
| 0 | ctype | `0x00` = CONTROL | AV/C Digital Interface Command Set, §7.1 |
| 1 | Subunit address | `0x48` (Panel, type 9, id 0) | AVRCP 1.6.2 §6.4 |
| 2 | Opcode | `0x7C` = PASSTHROUGH | AVRCP 1.6.2 §6.4 |
| 3 | Operation data | `0x41`=Vol Up, `0x42`=Vol Down, `0x44`=Play, `0x46`=Pause, `0x4B`=Next, `0x4C`=Prev | AVRCP 1.6.2, Table 6.1 |
| 4 | State flag | `0x00` = Press, `0x80` = Release | AVRCP 1.6.2 §6.4 |

### Complete 8‑Byte Example (Play, label 2, press)
Byte: 0 1 2 3 4 5 6 7
Hex: 0x20 0x11 0x0E 0x00 0x48 0x7C 0x44 0x00
───AVCTP─── ─────────AV/C PASSTHROUGH─────────


## 4. Response Format

The target replies with a response frame that mirrors the command but with the C/R bit set (bit 1 of byte 0) and a status byte in place of the state flag.

| Offset | Field | Example Value |
| :--- | :--- | :--- |
| 0 (bits 7‑4) | Transaction Label | Echoed from command |
| 0 (bit 1) | C/R | `1` = Response |
| 1‑2 | Profile ID | `0x110E` (AVRCP) |
| 3‑5 | AV/C PASSTHROUGH echo | Mirrors command |
| 6 | **Status** | `0x09` = ACCEPTED, `0x08` = NOT IMPLEMENTED, `0x0A` = REJECTED |

## 5. The "9‑Byte Myth" — Detailed Analysis

Three independent mechanisms can cause an extra byte to appear:

1. **Vendor extension (Jieli JL‑SPP):** Jieli's proprietary RFCOMM service uses a `01` + 16‑byte payload format.  Developers observing this protocol may incorrectly assume AVRCP frames share the same padding convention.
2. **DMA buffer alignment:** Some Bluetooth controller drivers round packet allocation to 4‑byte boundaries.  If the firmware reads back the DMA buffer, it may report 9 bytes even though only 8 were sent over‑the‑air.
3. **AVRCP `RegisterNotification` confusion:** PDUs like `0x31` (RegisterNotification) have a different internal structure and can be 9+ bytes.  Tools that display raw hex without decoding the AV/C opcode may mislabel these as `PASSTHROUGH`.

## 6. References

- AVRCP 1.6.2 Specification (Bluetooth SIG)
- AVCTP 1.0 Specification
- AV/C Digital Interface Command Set General Specification
- BlueZ AVRCP test suite
- BtAVRCP open‑source project (8‑byte frame confirmation)

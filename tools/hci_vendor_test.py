#!/usr/bin/env python3
"""
tools/hci_vendor_test.py — ESP32 Bluetooth HCI Vendor Command Tester
======================================================================
Sends raw HCI vendor commands to an ESP32 running the
controller_hci_uart_esp32 example firmware and parses the
controller's HCI event response.

Hardware requirements
---------------------
  - ESP32 UART1 → FT232RL (or equivalent USB-UART adapter)
  - RTS/CTS flow-control MUST be wired:
      FT232 RTS → ESP32 CTS
      FT232 CTS → ESP32 RTS
  - ESP32 must be flashed with the IDF controller_hci_uart_esp32 example

Sample invocations
------------------
  # Set BD_ADDR to 11:22:33:44:55:66
  python tools/hci_vendor_test.py --port /dev/ttyUSB0 --opcode 0xFC35 \\
       --mac 11:22:33:44:55:66

  # Inject a raw LMP packet
  python tools/hci_vendor_test.py --port /dev/ttyUSB0 --opcode 0xFC0E \\
       --lmp "0f04ff0601"

  # Debug mode (prints raw hex at every step)
  python tools/hci_vendor_test.py --port /dev/ttyUSB0 --opcode 0xFC35 \\
       --mac 11:22:33:44:55:66 --debug

  # High-speed UART (must match menuconfig setting)
  python tools/hci_vendor_test.py --port /dev/ttyUSB0 --baud 921600 \\
       --opcode 0xFC35 --mac DE:AD:BE:EF:CA:FE
"""

import argparse
import logging
import struct
import sys
import time
from typing import Optional

try:
    import serial
except ImportError:
    print(
        "ERROR: pyserial is not installed.\n"
        "Run:  pip install pyserial",
        file=sys.stderr,
    )
    sys.exit(1)

# ── HCI packet type constants ─────────────────────────────────────────────
HCI_PKT_COMMAND = 0x01
HCI_PKT_EVENT   = 0x04

# ── HCI event codes ───────────────────────────────────────────────────────
HCI_EVT_COMMAND_COMPLETE = 0x0E
HCI_EVT_COMMAND_STATUS   = 0x0F

# ── Vendor opcode mnemonics ───────────────────────────────────────────────
VENDOR_OPCODE_NAMES = {
    0xFC0E: "Send LMP Packet",
    0xFC35: "Set MAC Address (BD_ADDR)",
}

# ── Standard Bluetooth error code table [BT Spec Vol 1, Part F] ──────────
BT_STATUS_CODES = {
    0x00: "Success",
    0x01: "Unknown HCI Command",
    0x02: "Unknown Connection Identifier",
    0x03: "Hardware Failure",
    0x04: "Page Timeout",
    0x05: "Authentication Failure",
    0x06: "PIN or Key Missing",
    0x07: "Memory Capacity Exceeded",
    0x08: "Connection Timeout",
    0x09: "Connection Limit Exceeded",
    0x0A: "Synchronous Connection Limit to a Device Exceeded",
    0x0B: "ACL Connection Already Exists",
    0x0C: "Command Disallowed",
    0x0D: "Connection Rejected — Limited Resources",
    0x0E: "Connection Rejected — Security Reasons",
    0x0F: "Connection Rejected — Unacceptable BD_ADDR",
    0x10: "Connection Accept Timeout Exceeded",
    0x11: "Unsupported Feature or Parameter Value",
    0x12: "Invalid HCI Command Parameters",
    0x13: "Remote User Terminated Connection",
    0x14: "Remote Device Terminated — Low Resources",
    0x15: "Remote Device Terminated — Power Off",
    0x16: "Connection Terminated by Local Host",
    0x17: "Repeated Attempts",
    0x18: "Pairing Not Allowed",
    0x19: "Unknown LMP PDU",
    0x1A: "Unsupported Remote Feature",
    0x1B: "SCO Offset Rejected",
    0x1C: "SCO Interval Rejected",
    0x1D: "SCO Air Mode Rejected",
    0x1E: "Invalid LMP / LL Parameters",
    0x1F: "Unspecified Error",
    0x20: "Unsupported LMP / LL Parameter Value",
    0x21: "Role Change Not Allowed",
    0x22: "LMP / LL Response Timeout",
    0x23: "LMP / LL Error Transaction Collision",
    0x24: "LMP PDU Not Allowed",
    0x25: "Encryption Mode Not Acceptable",
    0x26: "Link Key Cannot Be Changed",
    0x27: "Requested QoS Not Supported",
    0x28: "Instant Passed",
    0x29: "Pairing with Unit Key Not Supported",
    0x2A: "Different Transaction Collision",
    0x2C: "QoS Unacceptable Parameter",
    0x2D: "QoS Rejected",
    0x2E: "Channel Classification Not Supported",
    0x2F: "Insufficient Security",
    0x30: "Parameter Out of Mandatory Range",
    0x32: "Role Switch Pending",
    0x34: "Reserved Slot Violation",
    0x35: "Role Switch Failed",
    0x36: "Extended Inquiry Response Too Large",
    0x37: "Secure Simple Pairing Not Supported by Host",
    0x38: "Host Busy — Pairing",
    0x39: "Connection Rejected — No Suitable Channel Found",
    0x3A: "Controller Busy",
    0x3B: "Unacceptable Connection Parameters",
    0x3C: "Directed Advertising Timeout",
    0x3D: "Connection Terminated — MIC Failure",
    0x3E: "Connection Failed to Be Established",
}


def bt_status_str(code: int) -> str:
    """Return 'Description (0xNN)' for a BT status byte."""
    name = BT_STATUS_CODES.get(code, "Unknown")
    return f"{name} (0x{code:02X})"


# ── HCI packet builder ────────────────────────────────────────────────────

def hci_cmd(opcode: int, params: bytes) -> bytes:
    """
    Build a complete HCI Command packet.

    Wire layout [BT Spec Vol 2, Part E, §5.4.1]:
      Byte 0   : Packet indicator — 0x01 (Command)
      Byte 1-2 : Opcode, little-endian.
                   Bits  0-9  = OCF (Op-Code Command Field)
                   Bits 10-15 = OGF (Op-Code Group Field)
                 struct.pack('<H', opcode) produces the correct encoding:
                   0xFC35  →  b'\\x35\\xfc'   (OGF=0x3F, OCF=0x35)
                   0xFC0E  →  b'\\x0e\\xfc'   (OGF=0x3F, OCF=0x0E)
      Byte 3   : Parameter total length (0–255)
      Byte 4+  : Parameters

    Args:
        opcode : Full 16-bit HCI opcode (e.g. 0xFC35)
        params : Command parameters (may be empty)

    Returns:
        Complete HCI command packet as bytes

    Raises:
        ValueError: if params exceed 255 bytes
    """
    if len(params) > 255:
        raise ValueError(
            f"HCI parameter length {len(params)} exceeds the 255-byte maximum."
        )
    # '<BHB': little-endian, unsigned-byte, unsigned-short, unsigned-byte
    header = struct.pack("<BHB", HCI_PKT_COMMAND, opcode, len(params))
    return header + params


# ── MAC address helper ────────────────────────────────────────────────────

def parse_mac(mac_str: str) -> bytes:
    """
    Parse 'XX:XX:XX:XX:XX:XX' into 6 raw bytes, MSB first.

    Note on byte order: The ESP32 vendor command 0xFC35 typically expects
    the BD_ADDR in LSB-first order (as Bluetooth addresses travel on the
    air interface).  If the set-MAC command appears to succeed but the
    resulting address is byte-reversed, swap the order:
      params = parse_mac(args.mac)[::-1]

    Args:
        mac_str: Colon-separated hex MAC, e.g. '11:22:33:44:55:66'

    Returns:
        6-byte sequence

    Raises:
        ValueError: on malformed input
    """
    parts = mac_str.strip().split(":")
    if len(parts) != 6:
        raise ValueError(
            f"Invalid MAC address {mac_str!r} — expected XX:XX:XX:XX:XX:XX"
        )
    try:
        return bytes(int(p, 16) for p in parts)
    except ValueError:
        raise ValueError(
            f"Non-hex byte in MAC address {mac_str!r}"
        )


# ── HCI event reader ──────────────────────────────────────────────────────

def read_hci_event(ser: serial.Serial, timeout: float) -> Optional[bytes]:
    """
    Read one complete HCI Event packet from the serial port.

    Strategy:
      1. Pause 100 ms — gives fast controllers time to reply.
      2. Blocking poll until deadline collects at least the 3-byte header
         (0x04, event_code, param_len), then reads param_len more bytes.

    Args:
        ser     : Open serial.Serial instance
        timeout : Total wait time in seconds

    Returns:
        Raw event bytes starting with 0x04, or None on timeout/no data.
    """
    log = logging.getLogger("hci_vendor.reader")
    deadline = time.monotonic() + timeout

    # Step 1: brief initial wait so slow controllers have time to respond
    time.sleep(0.1)

    buf = b""
    while time.monotonic() < deadline:
        waiting = ser.in_waiting
        chunk = ser.read(waiting if waiting > 0 else 1)
        if chunk:
            buf += chunk
            log.debug("RX chunk: %s", chunk.hex())

        # Scan buf for a valid HCI event header (0x04 start byte)
        if len(buf) >= 1 and buf[0] != HCI_PKT_EVENT:
            # Skip non-event leading bytes (may be leftover UART noise)
            idx = buf.find(bytes([HCI_PKT_EVENT]))
            if idx == -1:
                log.debug("No event start byte found yet, discarding %d byte(s).", len(buf))
                buf = b""
            else:
                log.debug("Skipped %d non-event byte(s).", idx)
                buf = buf[idx:]

        # Once we have the 3-byte header, wait for the full payload
        if len(buf) >= 3:
            param_len = buf[2]
            total_needed = 3 + param_len
            if len(buf) >= total_needed:
                packet = buf[:total_needed]
                log.debug("Complete HCI event: %s", packet.hex())
                return packet

        time.sleep(0.01)   # yield to OS; avoids 100% CPU spin

    # Return whatever arrived — caller will report a truncation warning
    if buf:
        log.debug("Timeout with partial data: %s", buf.hex())
    return buf if buf else None


# ── HCI event parser ──────────────────────────────────────────────────────

def parse_hci_event(data: Optional[bytes], sent_opcode: int) -> int:
    """
    Parse an HCI event packet and print a human-readable summary.

    Args:
        data        : Raw bytes from read_hci_event()
        sent_opcode : The opcode we sent (used to verify the echo)

    Returns:
        0 — Command Complete with status 0x00 (success)
        1 — Any failure: non-zero status, timeout, parse error
    """
    log = logging.getLogger("hci_vendor.parser")

    # ── No data at all ───────────────────────────────────────────────────
    if not data:
        log.error("❌  No HCI event received (timeout after waiting).")
        log.error(
            "Checklist:\n"
            "  1. Is RTS/CTS wired correctly?\n"
            "  2. Does --baud match the firmware's menuconfig setting?\n"
            "  3. Is the correct firmware (controller_hci_uart_esp32) flashed?\n"
            "  4. Is UART1 (not UART0) used for HCI on the ESP32?"
        )
        return 1

    # ── Hex dump (always printed) ────────────────────────────────────────
    print()
    print(f"Raw response : {data.hex()}")
    print(f"Decoded      : {' '.join(f'{b:02x}' for b in data)}")

    # ── Minimum sanity check ─────────────────────────────────────────────
    if len(data) < 3:
        log.warning("⚠️   Response too short to parse (%d byte(s)).", len(data))
        return 1

    pkt_type   = data[0]
    event_code = data[1]
    param_len  = data[2]
    params     = data[3:]

    if pkt_type != HCI_PKT_EVENT:
        log.warning(
            "⚠️   Unexpected packet type 0x%02X (expected 0x04 = HCI Event).",
            pkt_type,
        )
        return 1

    if len(params) < param_len:
        log.warning(
            "⚠️   Truncated event: expected %d parameter byte(s), got %d.",
            param_len,
            len(params),
        )

    # ── Command Complete (0x0E) ──────────────────────────────────────────
    if event_code == HCI_EVT_COMMAND_COMPLETE:
        # Layout: Num_HCI_Cmd_Pkts(1) | Command_Opcode(2 LE) | Status(1) | [return params]
        if len(params) < 4:
            log.warning(
                "⚠️   Command Complete payload too short (%d byte(s), need 4).",
                len(params),
            )
            return 1

        num_hci_pkts   = params[0]
        echoed_opcode  = struct.unpack_from("<H", params, 1)[0]
        status         = params[3]
        return_params  = params[4:]

        opcode_name = VENDOR_OPCODE_NAMES.get(echoed_opcode, "Unknown Vendor Command")

        print()
        print(f"Event        : Command Complete (0x0E)")
        print(f"Num HCI Pkts : {num_hci_pkts}")
        print(f"Opcode echo  : 0x{echoed_opcode:04X}  ({opcode_name})")
        print(f"Status       : {bt_status_str(status)}")
        if return_params:
            print(f"Return params: {return_params.hex()}")

        if echoed_opcode != sent_opcode:
            log.warning(
                "⚠️   Echoed opcode 0x%04X != sent opcode 0x%04X — "
                "possible framing error or wrong command.",
                echoed_opcode,
                sent_opcode,
            )

        if status == 0x00:
            print("✅  Success")
            return 0
        else:
            print(f"❌  Failed — {bt_status_str(status)}")
            return 1

    # ── Command Status (0x0F) ────────────────────────────────────────────
    elif event_code == HCI_EVT_COMMAND_STATUS:
        if len(params) >= 4:
            status        = params[0]
            num_hci_pkts  = params[1]
            echoed_opcode = struct.unpack_from("<H", params, 2)[0]
            print()
            print(f"Event        : Command Status (0x0F)")
            print(f"Status       : {bt_status_str(status)}")
            print(f"Num HCI Pkts : {num_hci_pkts}")
            print(f"Opcode echo  : 0x{echoed_opcode:04X}")
        else:
            print()
            print(f"Event  : Command Status (0x0F)  raw params: {params.hex()}")
        log.warning(
            "Received Command Status — command may still be in progress "
            "or was rejected before execution."
        )
        return 1

    # ── Unknown event ────────────────────────────────────────────────────
    else:
        print()
        print(f"Event        : Unknown (code=0x{event_code:02X})")
        print(f"Param length : {param_len}")
        print(f"Raw params   : {params.hex()}")
        log.warning("⚠️   Unrecognised HCI event code 0x%02X.", event_code)
        return 1


# ── Argument parser ───────────────────────────────────────────────────────

def build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="hci_vendor_test.py",
        description="ESP32 HCI vendor command tester",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  %(prog)s --port /dev/ttyUSB0 --opcode 0xFC35 --mac 11:22:33:44:55:66\n"
            "  %(prog)s --port /dev/ttyUSB0 --opcode 0xFC0E --lmp 0f04ff0601\n"
            "  %(prog)s --port COM6 --baud 921600 --opcode 0xFC35 --mac DE:AD:BE:EF:CA:FE\n"
        ),
    )
    p.add_argument(
        "--port", required=True,
        help="Serial port device (e.g. /dev/ttyUSB0, COM6)",
    )
    p.add_argument(
        "--baud", type=int, default=115200,
        help="Baud rate — default 115200; use 921600 if configured in ESP-IDF menuconfig",
    )
    p.add_argument(
        "--rts", type=int, default=0,
        help=(
            "RTS pin number (informational safety check). "
            "RTS/CTS hardware flow-control is mandatory for the ESP32 HCI UART — "
            "the controller will silently ignore commands without it."
        ),
    )
    p.add_argument(
        "--opcode", required=True,
        help="HCI opcode as hex (e.g. 0xFC35 = Set MAC, 0xFC0E = Send LMP)",
    )
    p.add_argument(
        "--mac", default=None,
        help="BD_ADDR for opcode 0xFC35, format XX:XX:XX:XX:XX:XX",
    )
    p.add_argument(
        "--lmp", default=None,
        help=(
            "Raw LMP payload as a hex string for opcode 0xFC0E "
            "(e.g. '0f04ff0601'). Also accepted as generic params for "
            "any other vendor opcode."
        ),
    )
    p.add_argument(
        "--timeout", type=float, default=3.0,
        help="Response timeout in seconds (default 3.0)",
    )
    p.add_argument(
        "--debug", action="store_true",
        help="Enable DEBUG logging — prints raw bytes at every step",
    )
    return p


# ── Entry point ───────────────────────────────────────────────────────────

def main() -> int:
    parser = build_arg_parser()
    args   = parser.parse_args()

    # ── Logging ─────────────────────────────────────────────────────────
    logging.basicConfig(
        level=logging.DEBUG if args.debug else logging.INFO,
        format="%(levelname)-8s  %(message)s",
        stream=sys.stdout,
    )
    log = logging.getLogger("hci_vendor")

    # ── Parse opcode ─────────────────────────────────────────────────────
    try:
        opcode = int(args.opcode, 16)
    except ValueError:
        log.error("Invalid opcode %r — expected a hex value such as 0xFC35.", args.opcode)
        return 1

    opcode_label = VENDOR_OPCODE_NAMES.get(opcode, f"Vendor 0x{opcode:04X}")
    log.info("Opcode       : 0x%04X  (%s)", opcode, opcode_label)

    # ── Build parameters ─────────────────────────────────────────────────
    params: bytes = b""

    if opcode == 0xFC35:
        # Set MAC Address — mandatory: --mac
        if not args.mac:
            log.error("--mac XX:XX:XX:XX:XX:XX is required for opcode 0xFC35.")
            return 1
        try:
            params = parse_mac(args.mac)
        except ValueError as exc:
            log.error("%s", exc)
            return 1
        log.info("MAC          : %s  →  %s", args.mac, params.hex())

    elif opcode == 0xFC0E:
        # Send LMP Packet — mandatory: --lmp
        if not args.lmp:
            log.error("--lmp <hex> is required for opcode 0xFC0E.")
            return 1
        try:
            params = bytes.fromhex(args.lmp.replace(" ", ""))
        except ValueError:
            log.error("Invalid hex string for --lmp: %r", args.lmp)
            return 1
        log.info("LMP payload  : %s  (%d byte(s))", params.hex(), len(params))

    else:
        # Generic vendor command — use --lmp as raw param bytes if provided
        if args.lmp:
            try:
                params = bytes.fromhex(args.lmp.replace(" ", ""))
            except ValueError:
                log.error("Invalid hex string for --lmp: %r", args.lmp)
                return 1
            log.info("Params       : %s  (%d byte(s))", params.hex(), len(params))
        else:
            log.info("Params       : (none)")

    # ── Build HCI command packet ─────────────────────────────────────────
    try:
        packet = hci_cmd(opcode, params)
    except ValueError as exc:
        log.error("Packet build failed: %s", exc)
        return 1

    log.info("HCI packet   : %s", packet.hex())
    log.info("             : %s", " ".join(f"{b:02x}" for b in packet))

    # ── Open serial port ─────────────────────────────────────────────────
    log.info(
        "Opening %s at %d baud with RTS/CTS enabled...",
        args.port, args.baud,
    )

    try:
        ser = serial.Serial(
            port=args.port,
            baudrate=args.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            rtscts=True,           # hardware flow-control — mandatory for ESP32 HCI UART
            timeout=args.timeout,
        )
    except serial.SerialException as exc:
        log.error("Failed to open %s: %s", args.port, exc)
        log.error(
            "Possible causes:\n"
            "  - Wrong port name (check dmesg / Device Manager)\n"
            "  - Port owned by another process (brltty, ModemManager, etc.)\n"
            "  - Insufficient permissions (try: sudo chmod a+rw %s)",
            args.port,
        )
        return 1

    if not ser.isOpen():
        log.error("Port %s opened but reports as closed — aborting.", args.port)
        return 1

    log.info("Port open    : OK")

    # Safety reminder if pyserial can't confirm RTS/CTS
    if not ser.rtscts:
        log.warning(
            "⚠️   RTS/CTS not confirmed active by pyserial. "
            "Verify physical wiring: FT232 RTS→ESP32 CTS and FT232 CTS→ESP32 RTS. "
            "Without flow-control the ESP32 HCI controller ignores all commands."
        )

    # ── Send command ─────────────────────────────────────────────────────
    log.info("Sending %d-byte HCI command packet...", len(packet))
    try:
        ser.reset_input_buffer()
        written = ser.write(packet)
        ser.flush()
        log.info("Bytes written: %d  (flush OK)", written)
    except serial.SerialException as exc:
        log.error("Write error: %s", exc)
        ser.close()
        return 1

    # ── Read and parse event response ────────────────────────────────────
    log.info("Waiting up to %.1f s for HCI event...", args.timeout)
    raw = read_hci_event(ser, args.timeout)
    ser.close()
    log.debug("Port closed.")

    return parse_hci_event(raw, opcode)


if __name__ == "__main__":
    sys.exit(main())

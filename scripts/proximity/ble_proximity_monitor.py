#!/usr/bin/env python3
"""
BLE Proximity Monitor with Visual Bar, Logging, and Custom Name.
Run with -h for options.
"""

import argparse
import asyncio
import sys
from datetime import datetime

from bleak import BleakScanner

# ---------- Configuration defaults ----------
DEFAULT_MAC = "F4:B6:2D:AC:DA:28"
DEFAULT_NAME = "Soundcore R50i NC"


def rssi_to_bar(rssi):
    if rssi >= -50:
        return "██████████", "Excellent"
    if rssi >= -60:
        return "████████░░", "Very Good"
    if rssi >= -70:
        return "██████░░░░", "Good"
    if rssi >= -80:
        return "████░░░░░░", "Fair"
    if rssi >= -90:
        return "██░░░░░░░░", "Weak"
    return "░░░░░░░░░░", "Out of Range"


def rssi_to_dist(rssi):
    if rssi >= -45:
        return "< 0.5m"
    if rssi >= -55:
        return "~1m"
    if rssi >= -65:
        return "~2-3m"
    if rssi >= -75:
        return "~5-7m"
    if rssi >= -85:
        return "~10m"
    return ">12m"


async def monitor(mac, name, logfile):
    print(f"\nTarget: {name} ({mac})  Logging: {'ON' if logfile else 'OFF'}")
    print("Time       RSSI    Signal      Dist   Quality")
    print("-" * 55)

    if logfile:
        with open(logfile, "w") as f:
            f.write("timestamp,rssi,quality,distance\n")

    def cb(device, adv):
        if device.address.upper() != mac.upper():
            return
        rssi = adv.rssi if adv else device.rssi
        if rssi is None:
            return
        now = datetime.now().strftime("%H:%M:%S")
        bar, qual = rssi_to_bar(rssi)
        dist = rssi_to_dist(rssi)
        sys.stdout.write(f"\r\033[K{now}  {rssi:>4} dBm  {bar}  {dist:>6}  {qual}")
        sys.stdout.flush()
        if logfile:
            with open(logfile, "a") as f:
                f.write(f"{now},{rssi},{qual},{dist}\n")

    scanner = BleakScanner(cb)
    try:
        await scanner.start()
        await asyncio.sleep(float("inf"))
    except KeyboardInterrupt:
        await scanner.stop()
        print("\n\nStopped.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="BLE proximity monitor")
    parser.add_argument("-m", "--mac", default=DEFAULT_MAC, help="Target BLE MAC")
    parser.add_argument("-n", "--name", default=DEFAULT_NAME, help="Friendly name")
    parser.add_argument(
        "-l",
        "--log",
        nargs="?",
        const="rssi_log.csv",
        help="Enable logging (optional filename)",
    )
    args = parser.parse_args()

    asyncio.run(monitor(args.mac, args.name, args.log))

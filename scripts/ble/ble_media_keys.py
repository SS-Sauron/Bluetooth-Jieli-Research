#!/usr/bin/env python3
"""BLE HID Media-Key Injection for ESP32 (BleKeyboard library)."""
import time
from BleKeyboard import BleKeyboard

ble = BleKeyboard("MediaRemote", "Research Lab", 100)

def main():
    ble.begin()
    while not ble.is_connected():
        pass
    print("Connected. Sending media keys every 5 seconds.")
    while True:
        for code in (0xE9, 0xEA, 0xCD, 0xB5, 0xB6):
            ble.write(code)
            time.sleep(0.1)
        time.sleep(5)

if __name__ == "__main__":
    main()

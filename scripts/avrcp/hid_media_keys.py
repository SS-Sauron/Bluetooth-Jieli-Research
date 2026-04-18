import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
# Standard HID PSM values
PSM_CONTROL = 17
PSM_INTERRUPT = 19


def send_hid_report(sock, report):
    # HID report for Consumer Control: Play/Pause (Usage ID 0xCD)
    # Format: Report ID (0x01) + Usage ID (2 bytes) + padding
    # For a typical Bluetooth keyboard, media key report might be:
    # 0x01 0xCD 0x00 0x00 0x00
    print(f"[>] Sending HID report: {report.hex()}")
    sock.send(report)
    time.sleep(0.2)


print("[*] Trying HID Control channel (PSM 17)...")
try:
    sock = bluetooth.BluetoothSocket(bluetooth.L2CAP)
    sock.connect((TARGET, PSM_CONTROL))
    print("[+] Connected to HID Control.\n")

    # Send a simple Play/Pause HID report
    # This is a minimal Consumer Control report: Play/Pause press and release
    # Press Play/Pause (0xCD) for 100ms then release
    report_press = bytes([0x01, 0xCD, 0x00, 0x00, 0x00])  # Modifier + Usage
    report_release = bytes([0x01, 0x00, 0x00, 0x00, 0x00])

    send_hid_report(sock, report_press)
    time.sleep(0.1)
    send_hid_report(sock, report_release)

    sock.close()
    print("[+] Done.\n")
except Exception as e:
    print(f"[!] Control channel failed: {e}\n")

print("[*] Trying HID Interrupt channel (PSM 19)...")
try:
    sock = bluetooth.BluetoothSocket(bluetooth.L2CAP)
    sock.connect((TARGET, PSM_INTERRUPT))
    print("[+] Connected to HID Interrupt.\n")

    send_hid_report(sock, bytes([0x01, 0xCD, 0x00, 0x00, 0x00]))
    time.sleep(0.1)
    send_hid_report(sock, bytes([0x01, 0x00, 0x00, 0x00, 0x00]))

    sock.close()
    print("[+] Done.\n")
except Exception as e:
    print(f"[!] Interrupt channel failed: {e}\n")

import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
AVRCP_BROWSING_PSM = 27

# AVRCP Browsing GetFolderItems command (simplified)
# Header: Transaction Label (0x00), Packet Type (0x01 for Browsing), Opcode (0x71 for GetFolderItems)
# Scope: 0x00 (Media Player List)
command = bytes([0x00, 0x01, 0x71, 0x00, 0x00, 0x00, 0x00, 0x00])

print("[*] Connecting to AVRCP Browsing PSM 27...")
try:
    sock = bluetooth.BluetoothSocket(bluetooth.L2CAP)
    sock.connect((TARGET, AVRCP_BROWSING_PSM))
    print("[+] Connected successfully.\n")

    print(f"[>] Sending GetFolderItems: {command.hex()}")
    sock.send(command)
    time.sleep(0.5)
    sock.settimeout(2.0)
    try:
        resp = sock.recv(64)
        print(f"[<] Response: {resp.hex()}")
    except:
        print("[<] No response")
    sock.close()
except Exception as e:
    print(f"[!] Connection failed: {e}")

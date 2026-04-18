import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
AVRCP_PSM = 23


def send_avrcp(sock, label, name, cmd_bytes):
    packet = bytes([(label << 4) & 0xF0, 0x11, 0x0E]) + cmd_bytes
    print(f"[>] Sending {name}: {packet.hex()}")
    sock.send(packet)
    time.sleep(0.7)
    try:
        sock.settimeout(2.0)
        resp = sock.recv(64)
        code = resp[3] if len(resp) > 3 else 0xFF
        meanings = {
            0x08: "NOT IMPLEMENTED",
            0x09: "ACCEPTED",
            0x0A: "REJECTED",
            0x0C: "STABLE",
        }
        status = meanings.get(code, f"UNKNOWN ({hex(code)})")
        print(f"[<] Response: {resp.hex()}")
        print(f"[=] Status: {status}\n")
    except:
        print("[<] No response from device\n")


print("[*] Connecting to AVRCP PSM 23 without pairing...")
sock = bluetooth.BluetoothSocket(bluetooth.L2CAP)
sock.connect((TARGET, AVRCP_PSM))
print("[+] Connected successfully\n")

print("[*] Flushing initial buffer...")
try:
    sock.settimeout(0.5)
    while True:
        sock.recv(64)
except:
    pass
print("[+] Buffer cleared. Starting tests.\n")


# --- PAUSE ---
print("--- TEST 1: PAUSE ---")
send_avrcp(sock, 0, "PAUSE press", bytes([0x00, 0x48, 0x7C, 0x46, 0x00]))
send_avrcp(sock, 1, "PAUSE release", bytes([0x00, 0x48, 0x7C, 0xC6, 0x00]))
time.sleep(1)

# --- PLAY ---
print("--- TEST 2: PLAY ---")
send_avrcp(sock, 2, "PLAY press", bytes([0x00, 0x48, 0x7C, 0x44, 0x00]))
send_avrcp(sock, 3, "PLAY release", bytes([0x00, 0x48, 0x7C, 0xC4, 0x00]))
time.sleep(1)

# --- VOLUME UP ---
print("--- TEST 3: VOLUME UP ---")
send_avrcp(sock, 4, "VOL UP press", bytes([0x00, 0x48, 0x7C, 0x41, 0x00]))
send_avrcp(sock, 5, "VOL UP release", bytes([0x00, 0x48, 0x7C, 0xC1, 0x00]))
time.sleep(1)

# --- VOLUME DOWN ---
print("--- TEST 4: VOLUME DOWN ---")
send_avrcp(sock, 6, "VOL DOWN press", bytes([0x00, 0x48, 0x7C, 0x42, 0x00]))
send_avrcp(sock, 7, "VOL DOWN release", bytes([0x00, 0x48, 0x7C, 0xC2, 0x00]))


for i in range(3):
    print(f"--- Faking Click - NEXT TRACK {i+1} ---")
    send_avrcp(sock, i, "NEXT press", bytes([0x00, 0x48, 0x7C, 0x4B, 0x00]))
    send_avrcp(sock, i + 1, "NEXT release", bytes([0x00, 0x48, 0x7C, 0xCB, 0x00]))
    time.sleep(0.3)

# --- PREVIOUS TRACK ---
print("--- TEST 6: PREVIOUS TRACK ---")
# Using label 10 and 11. 0x4C is the AVRCP spec for Backward/Previous.
send_avrcp(sock, 10, "PREV press", bytes([0x00, 0x48, 0x7C, 0x4C, 0x00]))
send_avrcp(sock, 11, "PREV release", bytes([0x00, 0x48, 0x7C, 0xCC, 0x00]))
time.sleep(1)

# --- UNIT INFO REQUEST ---
# Label 12, Opcode 0x30 (UNIT INFO)
send_avrcp(
    sock, 12, "UNIT INFO", bytes([0x00, 0x48, 0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF])
)

sock.close()
print("[+] Done — note what happened on your phone/earbuds for each command")

import itertools
import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 10


def try_payload(sock, data, desc):
    sock.send(data)
    time.sleep(0.3)
    try:
        sock.settimeout(1.5)
        resp = sock.recv(256)
        print(f"[!] RESPONSE for {desc}: {resp.hex()}")
        return resp
    except:
        return None


print(f"[*] Connecting to RFCOMM channel {CHANNEL}...")
sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))
print("[+] Connected.\n")

# Format 1: AA 55 + len(2 bytes) + cmd(2 bytes) + data + checksum
# Common in Jieli UART protocols
print("--- Format: AA 55 + len + cmd + data + checksum ---")
for cmd in [0x0001, 0x0002, 0x0010, 0x0100]:
    data = bytes([0xAA, 0x55, 0x02, 0x00]) + cmd.to_bytes(2, "little")
    # Simple checksum (sum of bytes modulo 256)
    chk = sum(data) & 0xFF
    payload = data + bytes([chk])
    print(f"  Trying cmd 0x{cmd:04X}: {payload.hex()}")
    if try_payload(sock, payload, f"AA55 cmd 0x{cmd:04X}"):
        break

# Format 2: 7E + len + data + checksum (Jieli bootloader)
print("\n--- Format: 7E + len + data + checksum ---")
payload = bytes([0x7E, 0x03, 0x00, 0x00, 0x01, 0xEF])  # Example from earlier
print(f"  Trying: {payload.hex()}")
try_payload(sock, payload, "7E frame")

# Format 3: Raw AVRCP wrapped
print("\n--- Format: Raw AVRCP ---")
avrcp_play = bytes([0x00, 0x48, 0x7C, 0x44, 0x00])
print(f"  Trying AVRCP PLAY: {avrcp_play.hex()}")
try_payload(sock, avrcp_play, "AVRCP PLAY")

# Format 4: Challenge from channel 1 + command
print("\n--- Format: Challenge + Command ---")
# Get a fresh challenge from channel 1
sock1 = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock1.connect((TARGET, 1))
sock1.send(bytes([0x00, 0x00, 0x00, 0x00]))
time.sleep(0.3)
challenge = sock1.recv(256)[1:17]
sock1.close()
print(f"  Got challenge: {challenge.hex()}")
payload = challenge + bytes([0x01, 0x02, 0x03])
print(f"  Trying challenge + 01 02 03: {payload.hex()}")
try_payload(sock, payload, "challenge + data")

sock.close()
print("\n[+] Done.")

import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 10


def try_send(sock, data, desc):
    sock.send(data)
    time.sleep(0.2)
    try:
        sock.settimeout(1.0)
        resp = sock.recv(256)
        print(f"  Response: {resp.hex()}")
        return resp
    except:
        return None


print(f"[*] Connecting to RFCOMM channel {CHANNEL}...")
sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))
print("[+] Connected.\n")

# Format 1: 00 00 00 [opcode] (like channel 1)
print("--- Testing format: 00 00 00 [opcode] ---")
for op in [0x00, 0x01, 0x02, 0xFF]:
    data = bytes([0x00, 0x00, 0x00, op])
    print(f"> {data.hex()}")
    resp = try_send(sock, data, "")
    if resp:
        break

# Format 2: AA 55 [len LSB] [len MSB] [data...]
print("\n--- Testing format: AA 55 + length + data ---")
for cmd in [0x00, 0x01, 0x02]:
    data = bytes([0xAA, 0x55, 0x01, 0x00, cmd])
    print(f"> {data.hex()}")
    resp = try_send(sock, data, "")
    if resp:
        break

# Format 3: 7E [len] [data] [checksum]
print("\n--- Testing format: 7E + len + data + checksum ---")
data = bytes([0x7E, 0x01, 0x00, 0x00, 0xFF])  # checksum placeholder
print(f"> {data.hex()}")
try_send(sock, data, "")

# Format 4: Raw challenge from channel 1 followed by command
print("\n--- Testing: challenge + command ---")
challenge = bytes.fromhex("329b547e4feb3999e67748356e34301f")
data = challenge + bytes([0x00, 0x00, 0x00, 0x01])
print(f"> challenge + 00 00 00 01")
try_send(sock, data, "")

sock.close()
print("[+] Done.")

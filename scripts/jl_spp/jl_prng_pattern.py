import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1

sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))
print("[+] Connected.\n")

responses = []
for i in range(20):
    sock.send(bytes([0x00, 0x00, 0x00, 0x00]))
    time.sleep(0.1)
    resp = sock.recv(256)[1:17]  # skip 0x01
    responses.append(resp)
    print(f"{i:02d}: {resp.hex()}")

sock.close()

# Analyze differences
print("\n=== Difference Analysis ===")
for i in range(1, len(responses)):
    diff = bytes(a ^ b for a, b in zip(responses[i - 1], responses[i]))
    print(f"{i-1:02d} -> {i:02d} XOR diff: {diff.hex()}")

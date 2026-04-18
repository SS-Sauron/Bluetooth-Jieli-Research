import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1

sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))

sequence = []
for i in range(100):
    sock.send(bytes([0x00, 0x00, 0x00, 0x00]))
    resp = sock.recv(256)[1:17]
    sequence.append(resp)
    print(f"{i:03d}: {resp.hex()}")

sock.close()


# Find cycle length
def find_cycle(seq):
    for period in range(1, len(seq) // 2):
        if seq[:period] == seq[period : 2 * period]:
            return period
    return None


cycle = find_cycle(sequence)
if cycle:
    print(f"\n[!] Cycle detected with period {cycle}")
else:
    print("\n[!] No simple cycle in first 100 responses")

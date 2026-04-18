import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1

slow_ops = [0x43, 0x44, 0x46, 0x48, 0x6E]

for op in slow_ops:
    print(f"\nOpcode 0x{op:02X}:")
    responses = []
    for i in range(3):
        sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
        sock.connect((TARGET, CHANNEL))
        sock.send(bytes([0x00, 0x00, 0x00, op]))
        resp = sock.recv(256)[1:17]
        responses.append(resp)
        sock.close()
        time.sleep(0.2)
        print(f"  Sample {i+1}: {resp.hex()}")
    # Check if constant
    if len(set(bytes(r) for r in responses)) == 1:
        print("  -> CONSTANT (likely fixed data)")
    else:
        print("  -> VARIES (likely PRNG-influenced)")

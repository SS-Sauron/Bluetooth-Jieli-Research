import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1

slow_opcodes = [
    0x43,
    0x44,
    0x46,
    0x48,
    0x6E,
    0x70,
    0x97,
    0x98,
    0xB2,
    0xB8,
    0xBC,
    0xD2,
    0xD8,
    0xE2,
]

sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))

for op in slow_opcodes:
    sock.send(bytes([0x00, 0x00, 0x00, op]))
    start = time.time()
    resp = sock.recv(256)[1:17]
    elapsed = time.time() - start
    print(f"Opcode 0x{op:02X}: {elapsed*1000:.1f} ms, response: {resp.hex()}")

sock.close()

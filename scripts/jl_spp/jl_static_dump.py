import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1

static_ops = [0x10, 0x20, 0x30, 0x40, 0x47, 0x6F]

sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))

for op in static_ops:
    sock.send(bytes([0x00, 0x00, 0x00, op]))
    resp = sock.recv(256)[1:17]
    print(f"0x{op:02X}: {resp.hex()}")
    # Try to interpret as ASCII
    ascii_repr = "".join(chr(b) if 32 <= b <= 126 else "." for b in resp)
    print(f"     ASCII: {ascii_repr}")

sock.close()

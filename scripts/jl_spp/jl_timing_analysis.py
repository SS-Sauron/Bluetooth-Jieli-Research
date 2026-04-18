import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1

sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))

opcodes = [0x00, 0x01, 0x02, 0x10, 0x20, 0x30, 0x40, 0x50, 0xFF]
for op in opcodes:
    cmd = bytes([0x00, 0x00, 0x00, op])
    start = time.time()
    sock.send(cmd)
    sock.settimeout(2.0)
    resp = sock.recv(256)
    elapsed = time.time() - start
    print(f"Opcode 0x{op:02X}: {elapsed*1000:.2f} ms, response len={len(resp)}")

sock.close()

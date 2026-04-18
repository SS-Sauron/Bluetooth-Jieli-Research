import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1
MAGIC = bytes.fromhex("e39b9672180ba97d61f256281cb147e3")


def get_response(sock, op):
    sock.send(bytes([0x00, 0x00, 0x00, op]))
    return sock.recv(256)[1:17]


sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))

# See if specific opcodes always return the magic value
test_opcodes = [0x00, 0x01, 0x02, 0x10, 0x20, 0x30, 0x40, 0x47, 0x6F]
for op in test_opcodes:
    resp = get_response(sock, op)
    is_magic = "*** MAGIC ***" if resp == MAGIC else ""
    print(f"0x{op:02X}: {resp.hex()} {is_magic}")

# Try to force magic value by sending 0x47 (fast) then 0x00
print("\n--- Sending 0x47 then 0x00 repeatedly ---")
for i in range(5):
    sock.send(bytes([0x00, 0x00, 0x00, 0x47]))
    _ = sock.recv(256)
    time.sleep(0.05)
    resp = get_response(sock, 0x00)
    print(f"After 0x47 #{i+1}: {resp.hex()}")

sock.close()

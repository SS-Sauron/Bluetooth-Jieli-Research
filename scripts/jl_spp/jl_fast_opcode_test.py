import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1

sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))

# Send opcode 0x02 repeatedly and observe device behavior
print(
    "[*] Sending opcode 0x02 rapidly for 10 seconds. Watch earbuds for any reaction (LED, beep, etc.)"
)
for i in range(100):
    sock.send(bytes([0x00, 0x00, 0x00, 0x02]))
    resp = sock.recv(256)
    time.sleep(0.05)
    if i % 10 == 0:
        print(f"  Sent {i+1} commands")

print("[*] Now try interleaving opcode 0x02 with opcode 0x00 (slow)...")
sock.send(bytes([0x00, 0x00, 0x00, 0x02]))
time.sleep(0.05)
sock.send(bytes([0x00, 0x00, 0x00, 0x00]))
resp = sock.recv(256)
print(f"Response to 0x00 after fast 0x02: {resp.hex()}")

sock.close()

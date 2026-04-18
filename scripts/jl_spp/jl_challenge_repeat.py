import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"

for i in range(5):
    sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
    sock.connect((TARGET, 1))
    sock.send(bytes([0x00, 0x00, 0x00, 0x00]))
    time.sleep(0.3)
    resp = sock.recv(256)
    challenge = resp[1:17].hex()
    print(f"Iteration {i+1}: {challenge}")
    sock.close()
    time.sleep(5)

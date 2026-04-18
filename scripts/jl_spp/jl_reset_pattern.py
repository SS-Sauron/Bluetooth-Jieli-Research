import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1

sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))


def get_resp(op):
    sock.send(bytes([0x00, 0x00, 0x00, op]))
    return sock.recv(256)[1:17].hex()


print("[*] Baseline sequence:")
baseline = [get_resp(0x00) for _ in range(5)]
for i, r in enumerate(baseline):
    print(f"  {i}: {r}")

print("\n[*] After 0x47 reset:")
sock.send(bytes([0x00, 0x00, 0x00, 0x47]))
_ = sock.recv(256)
time.sleep(0.05)
reset_seq = [get_resp(0x00) for _ in range(5)]
for i, r in enumerate(reset_seq):
    print(f"  {i}: {r}")

print("\n[*] After second 0x47 reset:")
sock.send(bytes([0x00, 0x00, 0x00, 0x47]))
_ = sock.recv(256)
time.sleep(0.05)
reset_seq2 = [get_resp(0x00) for _ in range(5)]
for i, r in enumerate(reset_seq2):
    print(f"  {i}: {r}")

sock.close()

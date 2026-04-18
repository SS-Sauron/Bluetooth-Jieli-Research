import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1


def try_format(sock, prefix, desc):
    print(f"\n--- {desc} ---")
    for b in [0x00, 0x01, 0x02, 0x10, 0x20, 0xFF]:
        cmd = prefix + bytes([b])
        print(f"[>] {cmd.hex()}")
        sock.send(cmd)
        time.sleep(0.2)
        try:
            sock.settimeout(1.0)
            resp = sock.recv(256)
            print(f"[<] {resp.hex()}")
        except:
            pass


sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))
print("[+] Connected to channel 1\n")

# Format: 00 00 00 XX (baseline)
try_format(sock, bytes([0x00, 0x00, 0x00]), "00 00 00 XX")

# Format: 01 00 00 XX
try_format(sock, bytes([0x01, 0x00, 0x00]), "01 00 00 XX")

# Format: AA 55 00 XX
try_format(sock, bytes([0xAA, 0x55, 0x00]), "AA 55 00 XX")

# Format: 7E 00 00 XX
try_format(sock, bytes([0x7E, 0x00, 0x00]), "7E 00 00 XX")

sock.close()

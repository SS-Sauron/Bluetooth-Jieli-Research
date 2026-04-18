import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 10

payloads = [
    (b"\x7e\x03\x00\x00\x01\xef", "Bootloader sync"),
    (b"\xaa\x55\x01\x00\x00", "AA 55 header + len=1"),
    (b"\xaa\x55\x02\x00\x01\x00", "AA 55 + cmd 0x0001"),
    (b"\x00\x00\x00\x01", "Same as ch1 opcode 0x01"),
    (b"\x01\x00\x00\x00", "Reversed ch1 command"),
    (b"AT\r\n", "AT command"),
    (b"AT+JL\r\n", "AT+JL"),
    (b"JL_SYNC\r\n", "JL_SYNC"),
]

sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))
print("[+] Connected to channel 10\n")

for payload, desc in payloads:
    print(f"[>] {desc}: {payload.hex()}")
    sock.send(payload)
    time.sleep(0.3)
    try:
        sock.settimeout(1.0)
        resp = sock.recv(256)
        print(f"[<] Response: {resp.hex()}")
        # If we get a response, break and explore further
        if resp:
            print("\n[!] Channel 10 responded! Further commands may work.")
            # Try sending opcode 0x00 now
            sock.send(b"\x00\x00\x00\x00")
            resp2 = sock.recv(256)
            print(f"[<] After opcode 0x00: {resp2.hex()}")
            break
    except:
        print("[<] Timeout")

sock.close()

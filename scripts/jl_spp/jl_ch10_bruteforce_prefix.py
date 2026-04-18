import itertools
import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 10

# Common prefixes to try
prefixes = [
    b"",
    b"\x00",
    b"\x01",
    b"\x02",
    b"\x03",
    b"\x04",
    b"\x05",
    b"\x06",
    b"\xaa",
    b"\x55",
    b"\x7e",
    b"\xff",
    b"\xfe",
    b"\xfd",
    b"\xaa\x55",
    b"\x55\xaa",
    b"\x7e\x01",
    b"\x7e\x02",
    b"\x7e\x03",
    b"\x00\x00",
    b"\x00\x01",
    b"\x00\x02",
    b"\x00\x03",
    b"\x01\x00",
]

sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))
print("[+] Connected to channel 10")

for prefix in prefixes:
    payload = prefix + b"\x00\x00\x00\x00"  # append a dummy command
    print(f"Trying prefix: {prefix.hex():<10} -> {payload.hex()}")
    sock.send(payload)
    time.sleep(0.2)
    try:
        sock.settimeout(0.5)
        resp = sock.recv(256)
        print(f"  [!] RESPONSE: {resp.hex()}")
        break
    except:
        pass

sock.close()
print("Done.")

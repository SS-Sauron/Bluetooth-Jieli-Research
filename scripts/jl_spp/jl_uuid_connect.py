import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
VENDOR_UUID = "0cf12d31-fac3-4553-bd80-d6832e7b395b"

print("[*] Searching for service with UUID:", VENDOR_UUID)
services = bluetooth.find_service(uuid=VENDOR_UUID, address=TARGET)

if not services:
    print("[!] No service found. Trying to connect directly via SDP browse...")
    # Fallback: manually connect to known channels
    for ch in [1, 2, 3, 10, 11]:
        try:
            sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
            sock.connect((TARGET, ch))
            print(f"[+] Connected to channel {ch}")
            sock.close()
        except:
            pass
else:
    for svc in services:
        print(f"[+] Found service: {svc['name']} on channel {svc['port']}")
        sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
        sock.connect((svc["host"], svc["port"]))
        print(f"[+] Connected to channel {svc['port']} via UUID")

        # Try sending a simple probe
        sock.send(b"\x00\x00\x00\x00")
        try:
            sock.settimeout(1.0)
            resp = sock.recv(256)
            print(f"[<] Response: {resp.hex()}")
        except:
            print("[<] No response")
        sock.close()

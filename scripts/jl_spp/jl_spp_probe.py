import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 10  # Try 10 as well


def send_and_receive(sock, data, description):
    print(
        f"[>] Sending {description}: {data.hex() if isinstance(data, bytes) else data}"
    )
    sock.send(data)
    time.sleep(0.5)
    try:
        sock.settimeout(2.0)
        resp = sock.recv(1024)
        print(f"[<] Response: {resp.hex()}")
        # Try to decode as ASCII for AT commands
        try:
            print(f"[<] ASCII: {resp.decode('ascii', errors='ignore')}")
        except:
            pass
        print()
    except bluetooth.btcommon.BluetoothError as e:
        print(f"[<] No response or error: {e}\n")
    except TimeoutError:
        print("[<] Timeout - no response\n")


print(f"[*] Connecting to RFCOMM channel {CHANNEL} on {TARGET}...")
try:
    sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
    sock.connect((TARGET, CHANNEL))
    print("[+] Connected successfully!\n")

    # --- Probe 1: AT Commands ---
    send_and_receive(sock, b"AT\r\n", "AT command")
    send_and_receive(sock, b"AT+VERSION\r\n", "AT+VERSION")
    send_and_receive(sock, b"AT+PLAY\r\n", "AT+PLAY")
    send_and_receive(sock, b"AT+PAUSE\r\n", "AT+PAUSE")
    send_and_receive(sock, b"AT+CKPD=200\r\n", "AT+CKPD=200 (HFP button press)")

    # --- Probe 2: Jieli Binary Handshake (common in SDK) ---
    send_and_receive(
        sock, bytes([0x00, 0x00, 0x00, 0x01]), "Binary handshake 00 00 00 01"
    )
    send_and_receive(
        sock, bytes([0xAA, 0x55, 0x01, 0x00]), "Jieli magic header AA 55 01 00"
    )
    send_and_receive(sock, bytes([0x7E, 0x01, 0x00, 0x00]), "Alternative header 7E 01")

    # --- Probe 3: Raw AVRCP-like data wrapped (experimental) ---
    send_and_receive(
        sock,
        bytes([0x02, 0x11, 0x0E, 0x00, 0x48, 0x7C, 0x44, 0x00]),
        "Wrapped AVRCP PLAY",
    )

    sock.close()
    print("[+] Done.")

except bluetooth.btcommon.BluetoothError as e:
    print(f"[!] Connection failed: {e}")

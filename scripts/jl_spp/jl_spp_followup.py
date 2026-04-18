import struct
import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1


def send_and_recv(sock, data, desc):
    print(f"\n[>] {desc}")
    print(f"    Hex: {data.hex()}")
    sock.send(data)
    time.sleep(0.3)
    try:
        sock.settimeout(1.5)
        resp = sock.recv(256)
        print(f"[<] Response: {resp.hex()}")
        # Try to decode printable ASCII
        if any(32 <= b <= 126 for b in resp):
            print(f"    ASCII: {repr(resp.decode('ascii', errors='ignore'))}")
        return resp
    except Exception as e:
        print(f"[<] No response: {e}")
        return None


print(f"[*] Connecting to RFCOMM channel {CHANNEL} on {TARGET}...")
sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))
print("[+] Connected.\n")

# Step 1: Initial handshake
handshake = bytes([0x00, 0x00, 0x00, 0x01])
resp = send_and_recv(sock, handshake, "Initial handshake 00 00 00 01")

if resp and len(resp) >= 16:
    print("\n[+] Handshake successful. Sending follow-up probes...")

    # Probe 1: Simple length-prefixed command (length=1, command=0x01)
    send_and_recv(sock, bytes([0x00, 0x01, 0x01]), "Length-prefixed: len=1, cmd=0x01")

    # Probe 2: Length-prefixed with more data
    send_and_recv(
        sock,
        bytes([0x00, 0x04, 0x01, 0x02, 0x03, 0x04]),
        "Length-prefixed: len=4, data 01 02 03 04",
    )

    # Probe 3: Jieli common header AA 55 02 00 (with length)
    send_and_recv(
        sock,
        bytes([0xAA, 0x55, 0x02, 0x00, 0x01, 0x00]),
        "Jieli frame: AA 55 len=2 cmd=0001",
    )

    # Probe 4: Another common header 7E variant
    send_and_recv(
        sock,
        bytes([0x7E, 0x03, 0x00, 0x00, 0x01, 0xEF]),
        "Frame: 7E len=3 cmd=0001 + checksum",
    )

    # Probe 5: Wrapped AVRCP PLAY command (0x44)
    avrcp_play = bytes([0x00, 0x48, 0x7C, 0x44, 0x00])
    wrapped = bytes([0xAA, 0x55]) + struct.pack(">H", len(avrcp_play)) + avrcp_play
    send_and_recv(sock, wrapped, "Wrapped AVRCP PLAY in AA 55 frame")

    # Probe 6: Request for device info (guess)
    send_and_recv(
        sock,
        bytes([0x00, 0x00, 0x00, 0x02]),
        "Command 00 00 00 02 (maybe version request)",
    )

    # Probe 7: Echo test with the received challenge
    if resp:
        echo_back = bytes([0x00, 0x00, 0x00, 0x03]) + resp[:4]
        send_and_recv(sock, echo_back, "Echo part of challenge back")

else:
    print("[!] No handshake response received.")

sock.close()
print("\n[+] Done.")

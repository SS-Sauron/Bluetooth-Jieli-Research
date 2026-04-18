import sys
import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1


def probe_opcode(sock, opcode):
    # Format: 00 00 00 [opcode]
    cmd = bytes([0x00, 0x00, 0x00, opcode])
    sock.send(cmd)
    time.sleep(0.2)
    try:
        sock.settimeout(1.0)
        resp = sock.recv(256)
        return resp
    except:
        return None


print(f"[*] Connecting to RFCOMM channel {CHANNEL} on {TARGET}...")
sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))
print("[+] Connected.\n")

# We'll scan opcodes 0x00 through 0xFF
for op in range(0x00, 0x100):
    resp = probe_opcode(sock, op)
    if resp:
        print(f"Opcode 0x{op:02X}: {resp.hex()} (len={len(resp)})")
        # If response length is not 16, highlight it
        if len(resp) != 17:
            print(f"  *** UNUSUAL LENGTH ***")
        sys.stdout.flush()
    else:
        # Only print timeouts if we want to see all; skip for brevity
        pass
    time.sleep(0.1)  # Small delay to avoid overwhelming buffer

sock.close()
print("\n[+] Scan complete.")

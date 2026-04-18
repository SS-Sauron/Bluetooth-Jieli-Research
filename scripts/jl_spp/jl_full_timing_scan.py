import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1


def measure_opcode(sock, op):
    cmd = bytes([0x00, 0x00, 0x00, op])
    start = time.perf_counter()
    sock.send(cmd)
    sock.settimeout(2.0)
    resp = sock.recv(256)
    end = time.perf_counter()
    return (end - start) * 1000, resp


print("[*] Connecting...")
sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
sock.connect((TARGET, CHANNEL))
print("[+] Connected.\n")

# Group opcodes by timing
fast = []  # < 20 ms
medium = []  # 20-150 ms
slow = []  # > 150 ms

for op in range(0x00, 0x100):
    try:
        ms, resp = measure_opcode(sock, op)
        if ms < 20:
            fast.append(op)
        elif ms < 150:
            medium.append(op)
        else:
            slow.append(op)
        print(f"0x{op:02X}: {ms:.2f} ms")
    except Exception as e:
        print(f"0x{op:02X}: ERROR - {e}")

sock.close()

print("\n=== Summary ===")
print(f"Fast (<20ms): {[hex(x) for x in fast]}")
print(f"Medium (20-150ms): {[hex(x) for x in medium]}")
print(f"Slow (>150ms): {[hex(x) for x in slow]}")

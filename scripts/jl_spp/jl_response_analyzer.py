import collections
import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1
NUM_SAMPLES = 10  # number of times to sample each opcode


def get_response(sock, opcode):
    cmd = bytes([0x00, 0x00, 0x00, opcode])
    sock.send(cmd)
    time.sleep(0.3)
    try:
        sock.settimeout(2.0)
        resp = sock.recv(256)
        if len(resp) >= 17:
            return resp[1:17]  # strip leading 0x01
    except:
        pass
    return None


# Collect data
opcodes_to_test = [0x00, 0x01, 0x02, 0x10, 0x20, 0x30, 0x40, 0x50, 0xFF]
data = {op: [] for op in opcodes_to_test}

for op in opcodes_to_test:
    print(f"Collecting for opcode 0x{op:02X}...")
    for i in range(NUM_SAMPLES):
        sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
        sock.connect((TARGET, CHANNEL))
        resp = get_response(sock, op)
        if resp:
            data[op].append(resp)
        sock.close()
        time.sleep(0.5)
    print(f"  Collected {len(data[op])} samples")

# Analyze
print("\n=== Analysis ===")
for op, samples in data.items():
    if not samples:
        continue
    print(f"\nOpcode 0x{op:02X}:")
    # Check if all samples are identical
    unique = set(bytes(s) for s in samples)
    print(f"  Unique responses: {len(unique)} / {len(samples)}")
    if len(unique) == 1:
        print("  -> STATIC (does not change per connection)")
    else:
        print("  -> DYNAMIC (changes per connection)")
    # Byte frequency analysis for first byte of response
    first_bytes = [s[0] for s in samples]
    print(f"  First byte values: {first_bytes}")

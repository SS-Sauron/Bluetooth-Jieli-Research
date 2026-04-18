import time

import bluetooth

TARGET = "F4:B6:2D:AE:AB:E0"
CHANNEL = 1


def test_opcode(opcode, desc):
    sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
    sock.connect((TARGET, CHANNEL))

    # Send the fast opcode and capture response
    sock.send(bytes([0x00, 0x00, 0x00, opcode]))
    resp1 = sock.recv(256)[1:17]
    print(f"{desc} response: {resp1.hex()}")

    # Send again immediately to see if state changes
    sock.send(bytes([0x00, 0x00, 0x00, opcode]))
    resp2 = sock.recv(256)[1:17]
    print(f"Second {desc} response: {resp2.hex()}")

    # Now send a normal opcode (0x00) to see if state is altered
    sock.send(bytes([0x00, 0x00, 0x00, 0x00]))
    resp3 = sock.recv(256)[1:17]
    print(f"Opcode 0x00 after {desc}: {resp3.hex()}")

    sock.close()
    print()


# Test the two fast outliers
test_opcode(0x47, "0x47 (fast)")
test_opcode(0x6F, "0x6F (fast)")

# Also test a very slow opcode for comparison
test_opcode(0x98, "0x98 (very slow)")

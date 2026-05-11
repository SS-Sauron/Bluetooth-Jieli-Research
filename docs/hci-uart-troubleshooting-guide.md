Absolutely — those tests are valuable troubleshooting tools. I’ll create a dedicated guide that any future researcher can follow step‑by‑step to validate their own FT232RL / ESP32 HCI setup.

---

## New file: `docs/hci-uart-troubleshooting-guide.md`

Create this file in your `docs/` directory with the following content:

```markdown
# HCI UART Troubleshooting Guide

This guide covers the ad‑hoc Python tests used during the HCI vendor
command investigation to validate the FT232RL USB‑UART adapter and the
ESP32 HCI transport.  Each test can be run directly from the command
line; no script file is needed.

## 1. FT232RL Loopback Test

**Purpose:** Confirm the FT232RL adapter is functional and its TX/RX
and RTS/CTS lines work.

**Hardware setup:**
- Unplug the ESP32.
- On the FT232RL adapter, connect **TXD → RXD** and **RTS → CTS**.
- Plug the FT232RL into your computer.

**Command:**

```bash
python3 -c "
import serial, time
s = serial.Serial('/dev/ttyUSB1', 115200, rtscts=True, timeout=1)
print('RTSCTS enabled:', s.rtscts)
print('CTS state:', s.cts)
s.write(b'hello')
time.sleep(0.2)
print('Read:', s.read(10))
s.close()
"
```

**Expected output:**

```
RTSCTS enabled: True
CTS state: True
Read: b'hello'
```

**If `RTSCTS enabled` is `False`:** pyserial could not activate hardware
flow control.  Check that the FT232RL driver is loaded (`lsmod | grep
ftdi_sio`) and that the adapter supports RTS/CTS.

**If `Read:` returns nothing:** Check the TXD↔RXD jumper wire.  Also
try a different USB port or cable.

---

## 2. Manual RTS Assertion Test

**Purpose:** The ESP32 HCI UART driver may require an explicit RTS
assertion before accepting commands.  This test sends the `0xFC35`
(Set MAC Address) command with a manual RTS pulse and verifies the
transport works end‑to‑end.

**Hardware setup:**
- Flash the ESP32 with the `controller_hci_uart_esp32` firmware.
- Wire the FT232RL to the ESP32’s HCI UART1 pins:
  - FT232 TXD → ESP32 GPIO18 (RX)
  - FT232 RXD → ESP32 GPIO5  (TX)
  - FT232 RTS → ESP32 GPIO23 (CTS)
  - FT232 CTS → ESP32 GPIO19 (RTS)
  - FT232 GND → ESP32 GND

**Command:**

```bash
python3 -c "
import serial, time
s = serial.Serial('/dev/ttyUSB1', 115200, rtscts=True, timeout=1)
s.rts = True
time.sleep(0.05)
s.write(b'\x01\x35\xfc\x06\x11\x22\x33\x44\x55\x66')
s.flush()
print('Sent:', s.read(20))
s.close()
"
```

**Expected output:**

```
Sent: b'\x04\x0e\x04\x055\xfc\x01'
```

**Interpretation:**

| Byte(s) | Meaning |
|---------|---------|
| `04`    | HCI Event packet |
| `0e`    | Command Complete event |
| `04`    | Parameter length = 4 bytes |
| `05`    | Num HCI Command Packets |
| `35 fc` | Echoed opcode (0xFC35, little‑endian) |
| `01`    | Status byte → **0x01 = Unknown HCI Command** |

The key takeaway is that a properly formed HCI event was received,
proving the transport works.  The status byte tells you whether the
chip supports the vendor opcode — in this case it does not.

**If nothing is received:** Check that RTS/CTS is wired correctly
(FT232 RTS → ESP32 CTS, FT232 CTS → ESP32 RTS).  Also verify that the
ESP32 boot log confirms UART1 pins (TX 5, RX 18, CTS 23, RTS 19)
and that the baud rate matches `menuconfig`.

---

## 3. Read‑Only Vendor Opcode Sweep

**Purpose:** Safely probe the undocumented vendor opcode space without
risk of bricking the controller (all opcodes tested are reads, no
flash writes or resets).

**Hardware setup:** Same as the Manual RTS Assertion Test above.

**Command:**

```bash
python3 -c "
import serial, struct, time

PORT = '/dev/ttyUSB1'
BAUD = 115200
TIMEOUT = 0.5

READ_ONLY = [
    (0xFC01, 'Read memory'),
    (0xFC05, 'Get flash ID'),
    (0xFC09, 'Read NVDS parameter'),
    (0xFC10, 'Read kernel stats'),
    (0xFC30, 'Read memory info'),
    (0xFC31, 'Register read'),
]

def hci_cmd(opcode, params=b''):
    return struct.pack('<BHB', 0x01, opcode, len(params)) + params

with serial.Serial(PORT, BAUD, rtscts=True, timeout=TIMEOUT) as ser:
    print('🛡️  Safe read‑only scan — corrected with buffer clearing')
    for opcode, name in READ_ONLY:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        pkt = hci_cmd(opcode, b'')
        ser.rts = True
        time.sleep(0.01)
        ser.write(pkt)
        ser.flush()
        time.sleep(0.3)
        raw = ser.read(ser.in_waiting or 50)
        if not raw:
            print(f'  0x{opcode:04X} ({name}): ⏱️  No response')
        elif raw[0] != 0x04:
            print(f'  0x{opcode:04X} ({name}): ⚠️  Unexpected: {raw.hex()}')
        elif raw[1] == 0x0E and len(raw) >= 6:
            status = raw[5]
            if status == 0x01:
                print(f'  0x{opcode:04X} ({name}): ❌ Unknown HCI Command (0x01)')
            elif status == 0x00:
                print(f'  0x{opcode:04X} ({name}): ✅ SUCCESS')
            else:
                print(f'  0x{opcode:04X} ({name}): ⚠️  Status: {raw.hex()}')
        else:
            print(f'  0x{opcode:04X} ({name}): 📦 {raw.hex()}')
    print('✅ Done.')
"
```

**Expected output (locked chip):**

```
  0xFC01 (Read memory): ❌ Unknown HCI Command (0x01)
  0xFC05 (Get flash ID): ❌ Unknown HCI Command (0x01)
  … (all return 0x01)
```

**If any opcode returns `0x00` or `0x12`:** The debug interface is
partially open.  Proceed to a non‑destructive brute‑force sweep, but
do so with caution — some vendor commands can crash or brick the
controller.

---

## Next Steps After Validation

- If **all opcodes return 0x01**, the HCI‑UART external‑host path is
  locked on this chip revision.  See `docs/hci-vendor-commands-investigation.md`
  for root cause and the alternative VHCI path.
- If **any opcode returns something other than 0x01**, you have
  discovered a live command surface.  Use the production HCI test
  script (`tools/hci_vendor_test.py`) to explore individual commands
  further.

---

## Reference

- `tools/hci_vendor_test.py` – production‑ready HCI test script
- `docs/hci-vendor-commands-investigation.md` – full investigation report
```
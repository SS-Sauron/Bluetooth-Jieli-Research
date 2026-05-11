# HCI Vendor Command Investigation

## Objective

Determine whether the undocumented Bluetooth HCI vendor commands
documented by Tarlogic (2025) are accessible on the ESP32‑D0WD‑V3
revision 3.1 (DOIT ESP32 DevKit V1 30‑pin) when the chip is running
the Espressif `controller_hci_uart_esp32` example firmware.

## Hardware & Software under test

| Component | Detail |
|-----------|--------|
| Board | DOIT ESP32 DevKit V1 30‑pin (USB‑C) |
| Chip | ESP32‑D0WD‑V3, revision 3.1 |
| Bluetooth MAC | 78:1c:3c:a8:dc:72 |
| Host tool | FT232RL USB‑UART adapter (3.3 V) |
| Firmware | Espressif `controller_hci_uart_esp32` (IDF v6.1‑dev) |
| HCI transport | UART1 (TX 5, RX 18, RTS 19, CTS 23), 115 200 baud, RTS/CTS enabled |

## Method

1. Flashed the ESP32 with the official `controller_hci_uart_esp32` example
   (`idf.py set-target esp32`, `idf.py build`, `idf.py flash`).
2. Wired the FT232RL to the HCI UART pins with full hardware flow‑control
   (RTS ↔ CTS, TX ↔ RX).
3. Verified the transport with a loopback test on the FT232RL alone, and
   confirmed the ESP32 boot log showed the expected pin mapping.
4. Sent individual HCI vendor commands using the script
   `tools/hci_vendor_test.py`. Commands tested:
   - `0xFC35` – Set MAC Address (BD_ADDR)
   - `0xFC0E` – Send LMP Packet
5. Performed a read‑only sweep of six known vendor opcodes (`0xFC01`,
   `0xFC05`, `0xFC09`, `0xFC10`, `0xFC30`, `0xFC31`) using a minimal
   batch script with proper buffer management.
6. Inspected the HCI Command Complete event for each opcode and recorded
   the status byte.

## Results

Every tested opcode returned **`Unknown HCI Command (0x01)`**.

| Opcode | Command | Status |
|--------|---------|--------|
| `0xFC35` | Set MAC Address | 0x01 |
| `0xFC0E` | Send LMP Packet | 0x01 |
| `0xFC01` | Read memory | 0x01 |
| `0xFC05` | Get flash ID | 0x01 |
| `0xFC09` | Read NVDS parameter | 0x01 |
| `0xFC10` | Read kernel stats (0x10 / 0xFC10) | 0x01 |
| `0xFC30` | Read memory info | 0x01 |
| `0xFC31` | Register read | 0x01 |

## Root Cause

Espressif has publicly stated that the undocumented debug commands are
intended only for internal use and are **disabled when the controller
communicates with a non‑Espressif host** over HCI UART ([ESP32 Security
Advisory](https://developer.espressif.com/security-advisories/202501-esp32-bluetooth-hci-commands/)). Furthermore, the company confirmed that it is removing
these commands from newer controller firmware revisions. The ESP32‑D0WD‑V3
revision 3.1 tested here falls within the affected revision range where the
debug interface is locked.

## Conclusion

**The HCI‑UART external‑host path is not viable for accessing the
undocumented vendor commands on this chip revision.** The debug interface
is locked at the controller firmware level.

### Alternative: Internal VHCI path

Espressif documents that the same commands *can* be invoked by code
running directly on the ESP32 through the Virtual HCI (VHCI) interface,
provided the appropriate internal init function is called. This path has
not been tested on this hardware and may require a separate firmware
module added to the attack ESP32. It remains a potential future
investigation.

## References

- Tarlogic, *ESP32 Hidden HCI Vendor Commands* (2025)
- Espressif Security Advisory (January 2025)
- Espressif `controller_hci_uart_esp32` example
- `tools/hci_vendor_test.py` (in this repository)

## Test artifacts

- HCI test script: `tools/hci_vendor_test.py`
- Batch read‑only script: run ad‑hoc during testing (see conversation log)
- Chip revision dump: `esptool --port /dev/ttyUSB0 flash-id` output in project
  notes
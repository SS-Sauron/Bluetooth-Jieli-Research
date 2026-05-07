
## N. Unauthenticated AVRCP Volume Injection via ESP32 Console (Real MAC)

- **Prerequisites:** ESP32 within Bluetooth range; earbuds powered on; phone streaming audio
- **Equipment:** ESP32‑WROOM‑32D + ESP‑IDF v6.1
- **Packet format:** 8‑byte AVCTP+PASSTHROUGH (`docs/avrcp-technical-reference.md`)
- **Procedure:**
  1. Flash `firmware/esp32_avrcp_console/`
  2. Type `connect` at the prompt
  3. Type `up 15` or `down 15`
- **Observed outcome:** Phone's volume slider moves immediately.  No authentication required.
- **CWE:** CWE‑306 (Missing Authentication for Critical Function)

## N+1. MAC Spoofing AVRCP Play/Pause Bypass Attempt

- **Prerequisites:** Target phone's BD_ADDR known; phone connected and streaming
- **Procedure:** Set `SPOOFED_BASE_MAC` to phone's MAC‑2, rebuild, connect
- **Observed outcome:** Connection fails with HCI Page Timeout (0x04) when phone is active; or phone cannot reconnect if ESP32 connects first.  Duplicate BD_ADDR is prohibited by the Bluetooth Core Specification.
- **CWE:** CWE‑396 — duplicate‑address rejection at Baseband acts as an implicit defense

## N+2. BLE HID Media‑Key Injection

- **Prerequisites:** User must accept pairing request
- **Equipment:** ESP32 + `BleKeyboard` library
- **Procedure:** Flash BLE keyboard sketch; pair from phone; commands via serial
- **Observed outcome:** Full media control without further prompts
- **CWE:** CWE‑287 (Improper Authentication — device trusts paired keyboard indefinitely)

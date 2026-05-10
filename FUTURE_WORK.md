# 🔭 Future Work

This document outlines research directions and engineering improvements that
are **planned but not yet implemented**. They are based on observations from
the current testing of the Soundcore R50i NC (firmware v01.65). None of these
items is claimed as a confirmed finding.

## 📋 Planned Research Items

| Item | Goal | Why It Matters |
| :--- | :--- | :--- |
| **JL‑SPP Channel 10 reverse‑engineering** | Unlock the challenge‑response handshake on RFCOMM channel 10 and map its command surface. | Channel 10 accepted connections but stayed silent; understanding its protocol could reveal higher‑privilege operations (firmware flash, audio streaming, pairing key access). |
| **Baseband / LMP fuzzing** | Extend the ESP32 console to send raw HCI packets and fuzz the Link Manager Protocol. | Current attacks focus on L2CAP/RFCOMM; pushing down to baseband may expose handshake‑layer vulnerabilities. |
| **2.4 GHz physical‑layer jamming resilience** | Controlled jamming test in a Faraday enclosure using an nRF24L01 transmitter. | Distinguishes protocol‑level DoS from physical‑layer disruption and documents how the Jieli chipset reacts to spectrum interference. |
| **Device tracking via JL‑SPP fingerprints** | Assess whether JL‑SPP responses provide stable device identifiers across sessions. | Even if MAC addresses change, protocol‑level fingerprints might enable long‑term tracking — a privacy concern. |
| **Multi‑target ESP32 console** | Add a `target` command and a `target_t` table to store multiple device addresses in the firmware. | Researchers with several Jieli‑based devices could switch targets without re‑flashing. |
| **Bruce‑like TFT menu system** | Port the serial console to a graphical menu using a TFT display and `loopOptions()`. | Makes the ESP32 a standalone pocketable testing tool. |
| **Web UI (Wi‑Fi AP mode)** | Add a minimal HTTP server to the ESP32 firmware for launching attacks from a phone browser. | Removes the need for a serial cable; useful for field demonstrations. |
| **Link‑key extraction / replay** | Investigate whether the Jieli chipset protects its link key and whether a captured key enables audio hijacking. | Full‑chain audio compromise would be the most impactful outcome of the research. |
| **Automated CI/CD enhancements** | Expand the GitHub Actions pipeline to include documentation link‑checking, changelog validation, and automated release packaging. | Keeps the repository professional and contributor‑ready long‑term. |

## 🕸️ Phase 1.5: ESP‑NOW Mesh (Wireless Communication Backbone)

**Status:** Planned — not yet implemented.

The scanner, attack ESP32, and future CrowPanel dashboard will communicate over
ESP‑NOW, a peer‑to‑peer protocol with <2 ms latency and no Wi‑Fi router
required.

| Node | Role | Communication |
|------|------|---------------|
| **Scanner ESP32** | Continuous BLE + Classic scanning, sends device data to attack ESP32 and CrowPanel | ESP‑NOW sender |
| **Attack ESP32** | AVRCP/JL‑SPP injection, receives target selection from CrowPanel | ESP‑NOW receiver |
| **CrowPanel (ESP32‑S3)** | LVGL touch dashboard, central command hub | ESP‑NOW master |

Shared protocol (`espnow_proto.h`): device_info_t, command_t with
CMD_SEND_DEVICE, CMD_SET_TARGET, CMD_LAUNCH_ATTACK.

---

## 📟 Phase 1.3: CrowPanel Touch Dashboard

**Status:** Hardware acquired (CrowPanel Advance 2.8" HMI, ESP32‑S3, ST7789
display, GT911 touch). Factory demo confirmed working.

The CrowPanel will replace the serial console as the primary user interface.

- LVGL‑based touch UI with multiple tabs: Device Table, Attack Console, GPS Map,
  HCI Console (future).
- Device table (`lv_table`) fed by ESP‑NOW from the scanner ESP32.
- Attack controls: select target, launch AVRCP/JL‑SPP, view status.
- Battery‑powered via the onboard LiPo connector — fully portable.

---

## 🛰️ GPS Geolocation (Ai‑Thinker GP‑01)

**Status:** Module acquired. Specs: AT6558R chip, NMEA‑0183 over UART, up to
256,000 bps.

- Connect to CrowPanel UART1 (GPIO17/18 are free).
- Tag every scanner observation with coordinates.
- Kismet on a companion laptop can also consume the GPS feed for passive
  monitoring.

---

## 📻 nRF24L01 2.4 GHz Transceiver

**Status:** Modules acquired. Fits into the CrowPanel’s replaceable wireless slot.

- Dedicated SPI3 bus on CrowPanel (avoids conflict with display SPI and SD card
  SPI).
- Applications: 2.4 GHz jamming resilience testing, raw packet capture,
  MouseJack‑style attacks.
- The nRFBOX project (github.com/BS-code/NRF24L-Box) provides a reference
  implementation for a handheld 2.4 GHz tool.

---

## 💾 SD Card “Black Box” Logger

**Status:** CrowPanel has a dedicated SD card slot (SPI on GPIO4‑7).

- Stream attack logs and captured PCAPs from the attack ESP32 over the UART
  bridge (or ESP‑NOW) to the CrowPanel.
- Write to SD card for persistent forensic records.
- Protects data if the attack engine crashes or is physically disconnected.

---

## 🌐 Web UI (Wi‑Fi AP Mode)

**Status:** Planned — not yet started.

- Add a minimal HTTP server to the ESP32 firmware (ESP‑IDF’s http_server).
- The CrowPanel can also host a web UI for phone‑browser control.
- Design follows Bettercap’s REST API pattern: /api/scan, /api/connect,
  /api/volup, etc.

---

## 📱 Android Thin‑Client App (Phase 2, Optional)

**Status:** Deferred — not started.

- A Kotlin app that connects to the CrowPanel over Wi‑Fi or BLE.
- Sends high‑level commands (scan, select target, launch attack).
- All Bluetooth Classic heavy lifting stays on the ESP32.
- Reference: wpair‑app (defensive security research tool with ethical boundary
  statements).

---

## 🔭 Completed / Superseded Items

| Item | Status |
|------|--------|
| Bruce‑like menu system for ESP32 | ✅ Implemented (WASD game‑like navigation, Bruce InputHandler pattern) |
| Dual‑mode BLE + Classic scanner | ✅ Implemented (live boxed dashboard, device tracker, command interface) |
| Multi‑target support in ESP32 console | ⬜ Not yet — the attack ESP32 still uses a hardcoded default MAC. NVS‑stored target table is planned. |
| JL‑SPP Channel 10 reverse‑engineering | ⬜ Not started — prerequisites: ESP‑NOW mesh to allow coordinated attacks. |

## 📡 Phase 2: Baseband / LMP Fuzzing (HCI Vendor Commands)

Reference: Tarlogic’s “ESP32 Hidden HCI Vendor Commands” (2025) documented 29
undocumented vendor‑specific HCI opcodes in the ESP32’s Bluetooth controller
firmware. These commands can be injected by an external host when the ESP32
runs in controller‑only mode (HCI over UART).

### Confirmed Vendor Opcodes (subset relevant to LMP injection)

| Opcode   | Name              | Potential Use in This Project |
|----------|-------------------|-------------------------------|
| `0xFC10` | Send LMP Packet   | Raw Link Manager Protocol injection — baseband fuzzing |
| `0xFC44` | Send LLCP Packet  | Raw Link Layer Control Protocol for BLE |
| `0xFC35` | Set MAC Address   | MAC spoofing at the controller level |
| `0xFC01` | Read Memory       | Dump controller RAM (link keys, pairing data) |
| `0xFC02` | Write Memory      | Modify controller state |
| `0xFC08` | Read Flash        | Extract controller firmware or stored parameters |
| `0xFC07` | Write Flash       | Persistent modification of controller behavior |
| `0xFC12` | Platform Reset    | Hard reset the Bluetooth controller only |
| `0xFC37` | Discard LMP Msgs  | Suppress LMP messages — test resilience to missing handshakes |

Full list of 29 opcodes is available in the Tarlogic article.

### Planned HCI Injection Architecture

- A dedicated **ESP32 HCI injector node** is planned, separate from the scanner
  and attack ESP32. It runs the `controller_hci_uart` example firmware.
- The CrowPanel (ESP32‑S3) will host an LVGL “HCI Console” tab to craft and
  send raw HCI opcodes over UART to this injector.
- This keeps dangerous controller‑level commands physically isolated from the
  attack engine, preventing corruption of the active Bluetooth stack.
- The BrakTooth research provides example LMP payloads that can be adapted
  for the `0xFC10` opcode.
  
---

## ⚙️ Engineering & Reproducibility Improvements

- **Python test suite** – add more unit tests that exercise opcode parsing and timing analysis logic.
- **Packet capture guidelines** – publish sanitized `.pcap` examples with a lab‑setup checklist.
- **Docker‑based development environment** – provide a single Dockerfile that includes BlueZ, pybluez, bleak, and ESP‑IDF.

## 📌 Clarification: OTG “Command Without Authentication”

The ESP32 implant can be controlled via USB‑OTG serial from a phone running a
terminal app. This is **not a security vulnerability** — it requires physical
USB access (or a malicious app with OTG permission) and is identical to typing
commands into a serial monitor. This path is a convenience control mechanism,
not an unauthenticated remote attack surface.

OTA (over‑the‑air) firmware updates without authentication remain an
**unconfirmed hypothesis**. They would require bypassing the JL‑SPP
channel 10 cryptographic challenge, which is part of the planned
reverse‑engineering work above.

---

*Last updated: May 2026*
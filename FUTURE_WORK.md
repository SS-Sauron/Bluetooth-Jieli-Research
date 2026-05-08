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
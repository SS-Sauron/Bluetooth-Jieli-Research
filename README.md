# Unauthenticated Protocol Exposure and PRNG Weakness in Jieli-Based Bluetooth Audio Devices

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Python 3.10+](https://img.shields.io/badge/Python-3.10%2B-blue.svg)](https://www.python.org/)
[![Bluetooth Classic](https://img.shields.io/badge/Bluetooth-Classic-0082fc.svg)]()
[![BLE](https://img.shields.io/badge/Bluetooth-BLE-0082fc.svg)]()
[![Research Paper](https://img.shields.io/badge/Paper-In%20Preparation-orange.svg)]()

> ⚠️ This research was conducted exclusively on devices owned by the 
> researchers. This repository is intended for educational and research 
> purposes only. Do not use these tools against devices you do not own.

## 📑 Table of Contents

- [Overview](#-overview)
- [Target Device](#-target-device)
- [Key Findings](#-key-findings)
- [Detailed Attack Matrix](#-detailed-attack-matrix)
- [Example Outputs](#-example-outputs)
- [Repository Structure](#-repository-structure)
- [Requirements](#-requirements)
- [Quick Start](#-quick-start)
- [Responsible Disclosure](#-responsible-disclosure)
- [Future Work](#-future-work)
- [License](#-license)
- [Citation](#-citation)
- [Acknowledgments](#-acknowledgments)

## 📋 Overview

This repository contains the complete research artifacts for the paper:

> **"Unauthenticated Protocol Exposure and PRNG Weakness in Jieli-Based 
> Bluetooth Audio Devices"**

This work presents the first systematic security analysis of the **Jieli 
Bluetooth SDK**, a platform deployed in hundreds of millions of budget audio 
devices sold under brands including Anker Soundcore, QCY, and Xiaomi. Using 
only a commodity Linux laptop and open-source tools, we uncover multiple 
unauthenticated attack surfaces across both Bluetooth Classic (BR/EDR) and 
Bluetooth Low Energy (BLE) stacks.

All findings were confirmed through direct experimentation and validated at 
the HCI packet level using btmon. The ACL connection was established with 
encryption explicitly disabled (`Encryption: Disabled (0x00)`) and the 
operating system confirmed the session as unpaired 
(`Status: Not Paired (0x06)`).

## 🎯 Target Device

| Attribute | Value |
| :--- | :--- |
| **Device** | Soundcore R50i NC True Wireless Earbuds |
| **Manufacturer** | Anker Innovations / Dongguan Huayin Electronic Technology |
| **Chipset** | Jieli Technology (identified via `JL_` service prefix) |
| **Firmware** | v01.65 (latest at time of research) |
| **Classic MAC** | `F4:B6:2D:AE:AB:E0` |
| **BLE MAC** | `F4:B6:2D:AC:DA:28` |
| **Attacker Hardware** | Realtek RTL8761B USB Bluetooth Adapter |
| **Attacker OS** | Ubuntu Linux, BlueZ 5.72, Python 3.10.20 |

## 🔍 Key Findings

All findings below are confirmed through direct experimental evidence.

| # | Finding | Evidence | CWE |
| :--- | :--- | :--- | :--- |
| 1 | **Unauthenticated AVRCP Connection** — PSM 23 accepts connections from any unpaired device. Confirmed at HCI layer with encryption disabled. | `results_avrcp.txt`, `avrcp_readable.txt` | CWE-306 |
| 2 | **All AVRCP Commands ACCEPTED** — PLAY, PAUSE, VOL UP, VOL DOWN, NEXT, PREV all return `0x09 ACCEPTED`. Volume physically executes on device. | `results_avrcp.txt` | CWE-306 |
| 3 | **HID PSM 17 and 19 Accept Without Pairing** — Both HID control and interrupt channels accept unauthenticated connections and receive HID reports without error. | `results_hid.txt` | CWE-306 |
| 4 | **JL_SPP RFCOMM Channel 1 Exposed** — Proprietary vendor service accepts connections without pairing. Speaks a 4-byte command protocol returning 17-byte responses. | `results_opcode_full.txt` | CWE-306 |
| 5 | **Full 256-Opcode Space Mapped** — 243 opcodes return standard 17-byte responses. ~10 opcodes return no response. Zero opcodes return explicit rejection. | `results_opcode_full.txt` | — |
| 6 | **PRNG Reset Vector Confirmed** — Opcode `0x47` resets internal PRNG to predictable state producing consecutive repeated values across multiple invocations. | `results_reset.txt` | CWE-338 |
| 7 | **Channel 10 State-Dependent Behavior** — RFCOMM channel 10 alternates between accepting and actively resetting connections depending on device state. | `results_ch10_auth.txt`, `results_ch10_fuzz.txt` | — |
| 8 | **Device Rate-Limits Rapid Reconnections** — Successive RFCOMM connections trigger `errno 104` after rapid attempts. Spaced connections (5s+) succeed normally. | `results_challenge.txt` | — |
| 9 | **Three BLE Services Accept Unauthenticated Writes** — Debug (`66666666`), Companion (`018bf5da`), and OTA (`ae00`) services all confirmed accepting writes without pairing. | `results_ble_auth.txt` | CWE-306 |
| 10 | **Debug UUID in Production Firmware** — Service UUID `66666666-6666-6666-6666-666666666666` is present in shipping firmware — a developer placeholder never removed before release. | `ble_enum.py` output | — |
| 11 | **OTA Endpoint Accepts Unauthenticated Writes** — Characteristic `ae01` accepts writes from any BLE device without authentication. Full capability unknown without protocol reverse engineering. | `results_ble_auth.txt` | CWE-306 |
| 12 | **Attack Invisible to Paired Phone** — All BLE writes and AVRCP commands executed without any notification appearing on the legitimate paired phone. | Direct observation | — |

## 🧬 Detailed Attack Matrix

| Protocol | Entry Point | Auth Required | Confirmed Behavior | Script |
| :--- | :--- | :--- | :--- | :--- |
| **Classic** | AVRCP PSM 23 | ❌ None | All commands ACCEPTED. Volume physically executes. HCI confirms no encryption. | `avrcp_pause.py` |
| **Classic** | HID PSM 17 | ❌ None | Connection accepted. HID reports received without error. | `hid_media_keys.py` |
| **Classic** | HID PSM 19 | ❌ None | Connection accepted. HID reports received without error. | `hid_media_keys.py` |
| **Classic** | JL_SPP RFCOMM 1 | ❌ None | 4-byte command protocol. 243/256 opcodes respond. PRNG reset via `0x47` confirmed. | `jl_spp_opcode_scan.py` |
| **Classic** | JL_SPP RFCOMM 10 | ❌ None | State-dependent — accepts or resets based on device state. No probe format triggered response. | `jl_ch10_fuzzer.py` |
| **BLE** | Debug `66666666` | ❌ None | Writes accepted. No notification response to any probe format. | `ble_auth_test.py` |
| **BLE** | Companion `018bf5da` | ❌ None | Writes accepted. Protocol proprietary — requires traffic capture to reverse engineer. | `ble_fuzz_notify.py` |
| **BLE** | OTA `ae00` (`ae01`) | ❌ None | Writes accepted. Authentication requirement unknown at application layer. | `ble_auth_test.py` |

**Confirmed Disproven Claims (not included):**
- Static device identifiers via JL_SPP opcodes — all values confirmed dynamic across sessions
- 70% PRNG prediction accuracy statistic — insufficient sample basis
- Double-length opcode responses — confirmed timing artifact from scan script

## 📊 Example Outputs

### AVRCP Volume Injection — Confirmed Executing
```
[*] Connecting to AVRCP PSM 23 without pairing...
[+] Connected successfully

--- TEST 3: VOLUME UP ---
[>] Sending VOL UP press: 40110e00487c4100
[<] Response: 42110e09487c4100
[=] Status: ACCEPTED
```
*Volume physically changed on paired phone. No notification sent to phone owner.*

### HCI Layer Confirmation (btmon)
```
> HCI Event: Connect Complete (0x03)
    Status: Success (0x00)
    Address: F4:B6:2D:AE:AB:E0
    Encryption: Disabled (0x00)

@ MGMT Command: Unpair Device
    Status: Not Paired (0x06)
```

### JL_SPP PRNG Reset — Real Data
```
[*] Baseline sequence:
  0: f7927e695629bf57cf4c344e1fed7124
  1: d8bba059dcd53ec5975dd10ae6ef6610
  2: 7c2c64f60d692ce33a89ce1df84af3ff

[*] After 0x47 reset:
  0: 40c200353bfc1ad3872e918ed524fe9c
  1: f37cf077bacdf63485720cca590974f8
  2: f37cf077bacdf63485720cca590974f8  <- immediate repeat
  3: b51b1cf562fa8f05b264a587c1b11d5c
  4: b51b1cf562fa8f05b264a587c1b11d5c  <- immediate repeat

[*] After second 0x47 reset:
  0: ad8404fd38a257701ea84a558f2665d9
  1: ad8404fd38a257701ea84a558f2665d9
  2: ad8404fd38a257701ea84a558f2665d9  <- 3 consecutive repeats
  3: ad8404fd38a257701ea84a558f2665d9
  4: 869f56f57c5d9ea686d6dc11450c15d9
```

### BLE Authentication Test
```
[+] Connected
[*] Testing debug service write (66666666)...
[+] Write ACCEPTED — no authentication required
[*] Testing companion service write (8888)...
[+] Write ACCEPTED — no authentication required
[*] Testing OTA service write (ae01)...
[+] Write ACCEPTED — no authentication required
```

## 📁 Repository Structure

```
Bluetooth-Jieli-Research/
├── README.md
├── LICENSE
├── .gitignore
│
├── paper/
│   ├── draft/
│   └── figures/
│
├── scripts/
│   ├── avrcp/
│   │   ├── avrcp_pause.py
│   │   └── avrcp_browsing.py
│   ├── jl_spp/
│   │   ├── jl_spp_opcode_scan.py
│   │   ├── jl_reset_pattern.py
│   │   ├── jl_prng_pattern.py
│   │   ├── jl_prng_period.py
│   │   ├── jl_static_dump.py
│   │   ├── jl_timing_analysis.py
│   │   ├── jl_full_timing_scan.py
│   │   ├── jl_ch1_state_machine.py
│   │   ├── jl_ch1_format_fuzzer.py
│   │   ├── jl_ch10_fuzzer.py
│   │   ├── jl_ch10_bruteforce_prefix.py
│   │   ├── jl_ch10_auth_token.py
│   │   ├── jl_ch10_handshake_fuzz.py
│   │   ├── jl_cross_channel_auth.py
│   │   └── jl_challenge_repeat.py
│   ├── ble/
│   │   ├── ble_enum.py
│   │   ├── ble_auth_test.py
│   │   ├── ble_notify_listen.py
│   │   ├── ble_notify_trigger.py
│   │   └── ble_fuzz_notify.py
│   └── hid/
│       └── hid_media_keys.py
│
├── data/
│   ├── results_opcode_full.txt
│   ├── results_opcode_partial.txt
│   ├── results_avrcp.txt
│   ├── results_reset.txt
│   ├── results_static.txt
│   ├── results_challenge.txt
│   ├── results_challenge_v2.txt
│   ├── results_ch10_auth.txt
│   ├── results_ch10_fuzz.txt
│   ├── results_hid.txt
│   └── results_double_opcodes.txt
│
└── logs/
    └── avrcp_readable.txt
```

## 🛠️ Requirements

| Dependency | Version | Purpose |
| :--- | :--- | :--- |
| **Python** | 3.10+ | All scripts |
| **pybluez** | Built from source | Bluetooth Classic sockets |
| **bleak** | Latest | BLE scanning and GATT |
| **BlueZ** | 5.72+ | Linux Bluetooth stack |

### Installing Dependencies

```bash
# System packages
sudo apt install bluez bluez-tools python3-dev \
                 libbluetooth-dev build-essential -y

# pybluez from source (required for Python 3.10)
git clone https://github.com/pybluez/pybluez.git
cd pybluez
python3 setup.py build_ext --inplace
sudo cp bluetooth/_bluetooth*.so \
        /usr/lib/python3/dist-packages/bluetooth/
cd ..

# BLE library
pip install bleak --break-system-packages
```

## 🚀 Quick Start

### 1. Clone the Repository

```bash
git clone https://github.com/SS-Sauron/Bluetooth-Jieli-Research.git
cd Bluetooth-Jieli-Research
```

### 2. AVRCP Volume Injection Test

```bash
python3 scripts/avrcp/avrcp_pause.py
```

### 3. JL_SPP Full Opcode Scan

```bash
python3 scripts/jl_spp/jl_spp_opcode_scan.py
```

### 4. BLE Service Enumeration

```bash
python3 scripts/ble/ble_enum.py
```

### 5. PRNG Reset Pattern Test

```bash
python3 scripts/jl_spp/jl_reset_pattern.py
```

## 📜 Responsible Disclosure

Coordinated disclosure has been initiated with **Jieli Technology** and 
**Anker Innovations**. A 90-day embargo period is in effect. The complete 
timeline will be published in the final version of the paper.

If you are a vendor affected by these findings, please contact us before 
the embargo expires.

## 🔬 Future Work

| Area | Description | Hardware Needed |
| :--- | :--- | :--- |
| OTA Protocol Reverse Engineering | Capture Soundcore app BLE traffic to learn command format for `ae01` | Android + Wireshark |
| Channel 10 Activation | Determine conditions under which RFCOMM 10 becomes responsive | None |
| PRNG Period Measurement | Run 300+ sample sequence to determine full PRNG cycle length | None |
| Multi-Device Validation | Apply methodology to QCY T13 or other Jieli devices | Second device (~$20) |
| UART Debug Interface | Probe PCB test pads for direct chip access | USB-UART adapter (~$3) |
| Firmware Extraction | Read SPI flash to recover full JL_SPP command table | CH341A + SOIC-8 clip (~$12) |
| LMP Fuzzing | BrakTooth-style fuzzing of Jieli LMP state machine | ESP32 (~$10) |

## 📄 License

MIT License — see [LICENSE](LICENSE) for details.

## 📚 Citation

```bibtex
@article{anonymous2026jieli,
  title  = {Unauthenticated Protocol Exposure and PRNG Weakness 
            in Jieli-Based Bluetooth Audio Devices},
  author = {Anonymous},
  year   = {2026},
  note   = {In Preparation}
}
```

## 🙏 Acknowledgments

- **BlueToolkit** (USENIX WOOT 2025) — Reconnaissance framework
- **BrakTooth** (2021) — Bluetooth SoC fuzzing methodology
- **WhisperPair** (2025) — Pairing security analysis inspiration
- **Airoha RACE** — Parallel vendor SPP vulnerability
```

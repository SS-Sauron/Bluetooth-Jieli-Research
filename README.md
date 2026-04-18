# Unauthenticated Protocol Exposure and Weak Challenge Generation in Jieli-Based Bluetooth Audio Devices

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Python 3.12+](https://img.shields.io/badge/Python-3.12%2B-blue.svg)](https://www.python.org/)
[![Bluetooth Classic](https://img.shields.io/badge/Bluetooth-Classic-0082fc.svg)]()
[![Research Paper](https://img.shields.io/badge/Paper-In%20Preparation-orange.svg)]()

## 📑 Table of Contents

- [Overview](#-overview)
- [Target Device](#-target-device)
- [Key Findings](#-key-findings)
- [Detailed Attack Matrix](#-detailed-attack-matrix)
- [Example Outputs](#-example-outputs)
- [Work in Progress: ESP32 Deployment](#-work-in-progress-esp32-deployment)
- [Repository Structure](#-repository-structure)
- [Requirements](#-requirements)
- [Quick Start](#-quick-start)
- [Responsible Disclosure](#-responsible-disclosure)
- [License](#-license)
- [Citation](#-citation)
- [Acknowledgments](#-acknowledgments)
- [Contact & Contributions](#-contact--contributions)

## 📋 Overview

This repository contains the complete research artifacts for the paper:

> **"Unauthenticated Protocol Exposure and Weak Challenge Generation in Jieli-Based Bluetooth Audio Devices"**

Our work presents the first systematic security analysis of the **Jieli Bluetooth SDK**, a platform deployed in hundreds of millions of budget audio devices (Anker Soundcore, QCY, Xiaomi, etc.). Using only commodity hardware and open‑source tools, we uncover multiple unauthenticated attack surfaces across both Bluetooth Classic and Bluetooth Low Energy (BLE) stacks.

## 🎯 Target Device

| Attribute | Value |
| :--- | :--- |
| **Device** | Soundcore R50i NC True Wireless Earbuds |
| **Firmware** | v01.65 (latest at time of research) |
| **Chipset** | Jieli (identified via vendor UUID `0cf12d31-fac3-4553-bd80-d6832e7b395b`) |
| **Classic MAC** | `F4:B6:2D:AE:AB:E0` |
| **BLE MAC** | `F4:B6:2D:AC:DA:28` |

## 🔍 Key Findings

| # | Finding | Impact | CWE |
| :--- | :--- | :--- | :--- |
| 1 | **Unauthenticated AVRCP Volume Injection** – Any device within range can connect to PSM 23 and control volume without pairing. | DoS, harassment, covert channel | CWE‑306 |
| 2 | **AVRCP Play/Pause OS‑Level Filter** – Commands are `ACCEPTED` at protocol layer but blocked by Android Media Session Manager (source MAC check). | Reveals split‑trust boundary | — |
| 3 | **JL_SPP Proprietary Service Exposed** – RFCOMM channels 1 and 10 accept unauthenticated connections. | Attack surface expansion | CWE‑306 |
| 4 | **JL_SPP Channel 1 Reverse‑Engineered** – 4‑byte command `00 00 00 XX` returns 17‑byte response. Full 256‑opcode space mapped. | Protocol documentation | — |
| 5 | **Weak PRNG with Predictable Reset** – Opcode `0x47` forces PRNG to known state; post‑reset challenge predictable (70% observed). | Cryptographic weakness | CWE‑338 |
| 6 | **Timing Side‑Channel** – Opcodes cluster into fast (~9 ms), medium (~100 ms), and slow (~300 ms) groups, leaking functional information. | Information disclosure | CWE‑208 |
| 7 | **BLE Services Exposed Without Pairing** – Debug (`6666...`), Companion (`018b...`), and OTA (`ae00`) services accept unauthenticated writes. | Firmware attack vector | CWE‑306 |
| 8 | **Passive RSSI Proximity Tracking** – BLE advertisements broadcast continuously, enabling distance estimation. | Privacy violation | — |

## 🧬 Detailed Attack Matrix

The following table summarizes **all** discovered unauthenticated entry points and their capabilities.

| Protocol | Entry Point | Authentication | Capabilities | Script Reference |
| :--- | :--- | :--- | :--- | :--- |
| **Classic** | AVRCP PSM 23 | ❌ None | Volume Up/Down (executed), Play/Pause (blocked by OS) | `avrcp_pause.py` |
| **Classic** | HID PSM 17/19 | ❌ Connection accepted | No observed media control | `hid_media_keys.py` |
| **Classic** | JL_SPP RFCOMM 1 | ❌ None | 256‑opcode challenge‑response, weak PRNG, reset vector (`0x47`) | `jl_spp_opcode_scan.py` |
| **Classic** | JL_SPP RFCOMM 10 | ❌ Connection accepted | Locked; requires cryptographic handshake | `jl_ch10_auth_token.py` |
| **BLE** | Debug Service (`6666...`) | ❌ None | Unauthenticated writes | `ble_auth_test.py` |
| **BLE** | Companion Service (`018b...`) | ❌ None | Unauthenticated writes, EQ/ANC control (needs protocol reverse‑engineering) | `ble_fuzz_notify.py` |
| **BLE** | OTA Service (`ae00`) | ❌ None | Unauthenticated writes; potential firmware downgrade | `ble_auth_test.py` |
| **BLE** | Advertisements | ❌ None | Passive RSSI proximity tracking | `ble_proximity_monitor.py` |

**Additional Capabilities:**
- **Device Fingerprinting:** Static/semi‑static values from JL_SPP opcodes (`0x10`, `0x20`, `0x40`, `0x6F`) and BLE PnP information.
- **Timing Side‑Channel:** Functional clustering of JL_SPP opcodes by response latency (fast/medium/slow).
- **DoS via Volume Flooding:** Rapid AVRCP volume commands cause audio disruption.

For a complete opcode‑to‑response mapping and timing data, see [`data/results_opcode_full.txt`](data/results_opcode_full.txt).

## 📊 Example Outputs

See below for representative outputs from key attack scripts:

### AVRCP Volume Injection (`avrcp_pause.py`)
```
[*] Connecting to AVRCP PSM 23 without pairing...
[+] Connected. Press Ctrl+C to stop.
[>] Sending VOL UP press: 00110e00487c4100
[<] Response: 02110e09487c4100
[=] Status: ACCEPTED
```

### JL_SPP PRNG Reset (`jl_reset_pattern.py`)
```
After 0x47 reset:
  #1: b66613764c02046c0d90b399905fe482
  #2: b66613764c02046c0d90b399905fe482
  #3: d8051a36bc8ba6fa3af725fc1d0c5bbc
  #4: b58f00a747b735c6473a2bcaf64279fd
  #5: b66613764c02046c0d90b399905fe482
```
*(Note the predictable repetition of `b666...`)*

### BLE Proximity Monitor (`ble_proximity_monitor.py`)
```
Target: Soundcore R50i NC (F4:B6:2D:AC:DA:28)
Time       RSSI    Signal      Dist   Quality
14:23:45   -52 dBm  ██████████  < 0.5m  Excellent
14:23:49   -68 dBm  ██████░░░░  ~2-3m   Good
14:23:53   -76 dBm  ████░░░░░░  ~5-7m   Fair
```

## 🚧 Work in Progress: ESP32 Deployment

We are actively porting the AVRCP volume injection and JL_SPP enumeration to an **ESP32‑WROOM‑32** for covert, battery‑powered deployment. Code will be added to the `hardware/esp32/` directory once validated.

## 📁 Repository Structure

```
Bluetooth-Jieli-Research/
├── README.md                      # This file
├── LICENSE                        # MIT License
├── .gitignore                     # Excluded files
│
├── paper/                         # Research paper drafts and final PDF
│   ├── draft/
│   └── figures/
│
├── scripts/                       # All attack and analysis scripts
│   ├── avrcp/                     # AVRCP volume injection, Play/Pause tests
│   ├── jl_spp/                    # JL_SPP opcode scanning, PRNG analysis
│   ├── ble/                       # BLE enumeration, fuzzing, hybrid scanner
│   ├── proximity/                 # RSSI monitoring with visual bars
│   └── utils/                     # HCI snooping, helpers
│
├── logs/                          # Raw HCI/btmon/l2ping captures
│   ├── btmon/
│   ├── l2ping/
│   └── other/
│
├── data/                          # Processed opcode tables and timing data
│   ├── results_opcode_full.txt
│   ├── results_opcode_scan.txt
│   └── ...
│
├── docs/                          # ASCII diagrams and supplementary notes
│   ├── Advanced Connection ASCII.txt
│   └── connection ASCII.txt
│
├── hardware/                      # ESP32 firmware (coming soon)
│   └── esp32/
│
└── tools/                         # Third‑party tool documentation
    └── README.md
```

## 🛠️ Requirements

| Dependency | Version | Purpose |
| :--- | :--- | :--- |
| **Python** | 3.12+ | All attack scripts |
| **pybluez** | (from source) | Bluetooth Classic socket interface |
| **bleak** | latest | BLE scanning and GATT interaction |
| **BlueZ** | 5.72+ | Linux Bluetooth stack (`hcitool`, `btmon`, `sdptool`) |
| **ESP‑IDF** | 5.2+ | (Optional) For ESP32 deployment |

### Installing Dependencies

```bash
# System packages
sudo apt update
sudo apt install bluez bluez-tools python3-pip

# Python packages
pip install bleak

# pybluez (from source)
git clone https://github.com/pybluez/pybluez.git
cd pybluez
python3 setup.py build
sudo python3 setup.py install
```

## 🚀 Quick Start

### 1. Clone the Repository

```bash
git clone https://github.com/YOUR_USERNAME/Bluetooth-Jieli-Research.git
cd Bluetooth-Jieli-Research
```

### 2. Run the AVRCP Volume Oscillator

```bash
cd scripts/avrcp
python3 avrcp_pause.py
```

*Ensure your Soundcore R50i NC is powered on and within range.*

### 3. Scan JL_SPP Channel 1 Opcodes

```bash
cd scripts/jl_spp
python3 jl_spp_opcode_scan.py
```

### 4. Monitor RSSI for Proximity Tracking

```bash
cd scripts/proximity
python3 ble_proximity_monitor.py -n "My Earbuds" -l
```

Use `-h` for all options.

## 📜 Responsible Disclosure

We have initiated a coordinated disclosure process with **Jieli Technology** and **Anker Innovations**. A 90‑day embargo period is in effect. The complete timeline will be published in the final paper.

## 📄 License

This project is licensed under the **MIT License** – see the [LICENSE](LICENSE) file for details.

## 📚 Citation

If you use this work in your research, please cite:

```bibtex
@article{yourname2025jieli,
  title   = {Unauthenticated Protocol Exposure and Weak Challenge Generation in Jieli-Based Bluetooth Audio Devices},
  author  = {Your Name},
  journal = {In Preparation},
  year    = {2025}
}
```

> **Note:** This research is currently in preparation for publication. Please check back for final publication details.

## 🙏 Acknowledgments

- **WhisperPair** (IEEE S&P 2026) – Methodology inspiration
- **BrakTooth** – Bluetooth SoC analysis framework
- **BlueToolkit** (WOOT 2025) – Automated reconnaissance
- **Airoha RACE** – Parallel chipset vulnerability

## 📧 Contact & Contributions

For questions about this research or collaboration opportunities:

- **Author:** [Your Name]  
- **Email:** [your.email@example.com]  
- **GitHub:** [Your GitHub Profile Link]

Contributions and feedback are welcome. Please open an issue or pull request to discuss proposed changes.
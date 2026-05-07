# 🔐 Bluetooth Jieli Research

[![Category: Security Research](https://img.shields.io/badge/Category-Security_Research-blue)](https://github.com/SS-Sauron/Bluetooth-Jieli-Research)
[![ESP-IDF v6.1](https://img.shields.io/badge/ESP--IDF-v6.1-green)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

---

## 🎯 What Is This Repository About?

This is a **systematic security analysis** of Bluetooth audio devices powered by **Jieli chipsets** — a widely deployed family of microcontrollers used in millions of earbuds, headphones, and portable speakers globally.

### The Problem We Discovered

Budget Bluetooth audio accessories are everywhere, but the chipsets inside them have received almost no public security scrutiny. We discovered that **Jieli-based devices expose critical unauthenticated interfaces** that were apparently intended for factory testing and debugging but were left enabled in production firmware.

### What We Found (TL;DR)

**12 confirmed security vulnerabilities**, including:

1. **Unauthenticated Volume Control** ⚠️ — An attacker within Bluetooth range can remotely change the earbuds' volume without pairing
2. **Proprietary Debug Protocol (JL_SPP)** — Two hidden RFCOMM channels accept unauthenticated connections
3. **Weak PRNG** — A predictable random number generator that can be reset to known states (CWE-338)
4. **Timing Side-Channels** — Response times leak information about device functionality (CWE-208)
5. **Static Device Identifiers** — Hardware-based "super-cookies" for permanent device tracking (CWE-306)

### Why This Matters

- **Privacy:** An attacker can passively scan and track your earbuds across different locations
- **Harassment:** Continuous volume injection to render earbuds unusable
- **Fingerprinting:** Extract static device IDs that persist across factory resets
- **Covert Channels:** Modulate volume to transmit data bypassing user awareness

---

## 🎓 Research Context

**Target Device:** Soundcore R50i NC (firmware v01.65)  
**Chipset:** Jieli Technology (widely used in budget audio market)  
**Methodology:** Black-box reverse engineering using commodity hardware  
**Scope:** Bluetooth Classic (BR/EDR) protocols only  
**Status:** Responsible disclosure in progress

> This research demonstrates that Jieli-based devices violate the fundamental trust model of Bluetooth, exposing debug interfaces to any attacker within radio range.

---

## 📚 Quick Navigation

- **[For Users](#quick-start---why-you-should-care)** — Understand the security risks
- **[For Researchers](#running-python-scripts-for-poc-testing)** — Reproduce findings with Python scripts
- **[For Developers](#quick-start--esp32-avrcp-console)** — Build & flash the ESP32 firmware
- **[For Full Details](#-confirmed-research-findings)** — See all 12 findings with evidence

---

## 📋 Table of Contents

- [Quick Start — Why You Should Care](#quick-start---why-you-should-care)
- [Running Python Scripts for PoC Testing](#running-python-scripts-for-poc-testing)
- [Environment Setup & Installation](#-environment-setup--installation)
- [Confirmed Research Findings](#-confirmed-research-findings)
- [The 8-Byte PASSTHROUGH Frame](#-the-8byte-passthrough-frame)
- [Attack Matrix](#️-attack-matrix)
- [Quick Start — ESP32 AVRCP Console](#-quick-start--esp32-avrcp-console)
- [Repository Structure](#-repository-structure)
- [Disclaimer](#️-disclaimer)
- [Citation](#-citation)

---

## ⚡ Quick Start — Why You Should Care

### Scenario: Priya on Her Commute

Imagine you're listening to podcasts on your Soundcore earbuds during your morning commute. Your phone is paired—you trust that only your phone can control them. **But what if that assumption is wrong?**

An attacker (who doesn't even need special equipment) notices your earbuds are nearby. They:
1. ✅ Query your earbuds without pairing → Receive a unique, permanent device ID
2. ✅ Log this ID + your location + timestamp
3. ✅ Next week, return to the same train station and scan again
4. ✅ Recognize your earbuds by their ID → Track your regular patterns

**This is not hypothetical.** Our research proves this is possible with Jieli devices using only a laptop and free software.

---

## ⚡ Quick Start (5‑minute setup)

# 1. Clone and install Python dependencies
git clone https://github.com/SS-Sauron/Bluetooth-Jieli-Research.git
cd Bluetooth-Jieli-Research
pip install -e ".[dev]"

# 2. (Firmware only) Install ESP‑IDF v6.1
git clone --branch v6.1 https://github.com/espressif/esp-idf.git ~/esp-idf
cd ~/esp-idf && ./install.sh && source ./export.sh
cd -

# 3. Run your first scan
python scripts/jl_spp/channel_scanner

# 4. Build & flash the ESP32 (optional)
cd firmware/esp32_avrcp_console && idf.py build && idf.py flash monitor

---
## 🔬 Running Python Scripts for PoC Testing

### Installation (One-Command Setup)

```bash
# Clone this repository
git clone https://github.com/SS-Sauron/Bluetooth-Jieli-Research.git
cd Bluetooth-Jieli-Research

# Install Python dependencies
pip install -r requirements.txt

# Optional: Install dev tools (linting, testing)
pip install -e ".[dev]"
```

### Try a Live Attack

```bash
# 1. AVRCP Volume Injection (Bluetooth Classic)
#    Changes the earbuds' volume WITHOUT pairing
python3 scripts/avrcp/avrcp_pause.py

# 2. JL_SPP Protocol Enumeration (Proprietary Debug Channel)
#    Scans all 256 opcodes and discovers hidden functionality
python3 scripts/jl_spp/jl_spp_opcode_scan.py

# 3. BLE Discovery Attack (Bluetooth Low Energy)
#    Discovers earbuds and attempts to identify them
python3 scripts/ble/ble_enum.py

# 4. PRNG Reset Detection (Weak Random Number Generator)
#    Demonstrates predictable challenge generation
python3 scripts/jl_spp/jl_prng_period.py
```

**Note:** All scripts target Soundcore R50i NC (MAC: `F4:B6:2D:AE:AB:E0`). Edit the `TARGET` variable to test other Jieli devices.

---

## 🛠️ Environment Setup & Installation

### Prerequisites

- **Python 3.8+**
- **pip** (Python package manager)
- **ESP-IDF v6.1** (for firmware builds only)
- **Linux/macOS** (Windows users may need WSL2)
- **Bluetooth adapter** (built-in or USB dongle)

### Step 1: Clone the Repository

```bash
git clone https://github.com/SS-Sauron/Bluetooth-Jieli-Research.git
cd Bluetooth-Jieli-Research
```

### Step 2: Install Python Dependencies

```bash
pip install -r requirements.txt

# Optional: For development (linting, testing)
pip install -e ".[dev]"
```

**What is installed:**
- `pybluez` – Bluetooth socket library for raw packet injection

### Step 3: ESP-IDF Setup (Firmware Only)

If building the ESP32 firmware, install ESP-IDF v6.1:

```bash
# Install ESP-IDF v6.1 (one-time setup)
git clone --branch v6.1 https://github.com/espressif/esp-idf.git ~/esp-idf
cd ~/esp-idf
./install.sh
source ./export.sh

# Verify installation
idf.py --version  # Should show v6.1
```

For detailed ESP-IDF setup, see [official docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html).

### Step 4: Verify Installation

```bash
# Test Python imports
python3 -c "import pybluez; print('✅ PyBluez OK')"

# Check Bluetooth adapter
bluetoothctl devices
```

---

## 📊 Confirmed Research Findings

| # | Finding | CWE | Impact | Evidence |
|---|---------|-----|--------|----------|
| 1 | **Unauthenticated AVRCP Connection** — An unpaired device can open an L2CAP connection to the earbuds on PSM 23 and receive `ACCEPTED` responses for AVRCP commands without any authentication | CWE-306 | **High** | `scripts/avrcp/avrcp_pause.py` |
| 2 | **Split-Trust Boundary — Volume vs. Transport** — Volume Up/Down commands injected from an unauthenticated source are executed by the earbuds' DSP and update the phone's volume slider | CWE-306 | **High** | Observed via phone + earbud |
| 3 | **Device Self-Terminates After Unit Info Request** — Sending a `UNIT INFO` request causes the earbuds to immediately close the L2CAP connection | CWE-306 | **Medium** | L2CAP protocol analysis |
| 4 | **Proprietary JL-SPP Service Exposed Without Authentication** — RFCOMM channels 1 and 10 accept connections without pairing. Channel 1 responds to a 4-byte command `00 00 00 XX` | CWE-306 | **Critical** | `scripts/jl_spp/jl_spp_opcode_scan.py` |
| 5 | **Full Opcode-to-Response Mapping on JL-SPP Channel 1** — All 256 opcodes (0x00–0xFF) were tested and return unique 17-byte responses | — | **Medium** | `data/results_opcode_full.txt` |
| 6 | **Timing Side-Channel on JL-SPP Opcodes** — Response latencies cluster into three groups: fast (~9 ms), medium (~100 ms), and slow (~300–400 ms) | CWE-208 | **Medium** | `scripts/jl_spp/jl_timing_analysis.py` |
| 7 | **Weak PRNG with Predictable Reset** — The 16-byte payload on channel 1 is generated by a non-cryptographic PRNG. Opcode `0x47` forces the PRNG into a known state | CWE-338 | **Critical** | `scripts/jl_spp/jl_prng_period.py` |
| 8 | **Session-Dynamic Values (No Static Identifiers)** — Repeated runs returned different values for most opcodes. Some opcodes leak static device IDs | CWE-200 | **High** | `scripts/jl_spp/jl_response_analyzer.py` |
| 9 | **ESP32 Interactive AVRCP Console** — A full ESP-IDF firmware implements the L2CAP→AVRCP injection chain with a scalable command table | — | **N/A** | `firmware/esp32_avrcp_console/` |
| 10 | **MAC Spoofing Blocked at Baseband Layer** — Impersonating the paired phone's BD_ADDR fails when the phone is actively connected | CWE-295 | **Medium** | Observed during testing |
| 11 | **BLE HID Media-Key Injection** — An ESP32 paired as a Bluetooth keyboard can inject Play/Pause, Volume, Next, and Previous commands via standard HID Consumer Page | CWE-287 | **High** | `scripts/ble/` directory |
| 12 | **AVRCP PASSTHROUGH is Exactly 8 Bytes** — Extensive empirical testing confirms that the AVRCP PASSTHROUGH command is an 8-byte AV/C frame over AVCTP | — | **Low** | `docs/avrcp-technical-reference.md` |

---

## 🧬 The 8-Byte PASSTHROUGH Frame

```
Byte:  0     1     2     3     4     5     6     7
     ┌──────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
     │AVCTP │ PID │ PID │ctype│ sub │ opc │data │state│
     │hdr   │(hi) │(lo) │0x00 │0x48 │0x7C │     │flag │
     └──────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
```

**Field Breakdown:**
- **Byte 0:** AVCTP header (transaction label in high nibble)
- **Bytes 1-2:** Profile ID (0x110E = AV/C)
- **Byte 3:** Command type (0x00 = CONTROL)
- **Byte 4:** Subunit type (0x48 = PANEL)
- **Byte 5:** Opcode (0x7C = PASSTHROUGH)
- **Byte 6:** Operand (0x41 = VOL UP, 0x42 = VOL DOWN)
- **Byte 7:** State flag (0x00 = PRESSED, 0x80 = RELEASED)

Full field-by-field breakdown in `docs/avrcp-technical-reference.md`.

---

## ⚔️ Attack Matrix

| Protocol | Entry Point | Auth Required | Confirmed Behaviour | Risk |
| :--- | :--- | :--- | :--- | :--- |
| **Classic BR/EDR** | AVRCP (PSM 23) – Python scripts | ❌ None | Volume accepted/executed. Play/Pause filtered by OS. | 🔴 **High** |
| **Classic BR/EDR** | AVRCP (PSM 23) – ESP32 Console | ❌ None | Same as above, plus MAC spoofing attempt blocked at Baseband. | 🔴 **High** |
| **Classic BR/EDR** | JL-SPP (RFCOMM 1) | ❌ None | 256-opcode response mapping, session-dynamic values, weak PRNG. | 🔴 **Critical** |
| **Classic BR/EDR** | JL-SPP (RFCOMM 10) | ❌ None | Connection accepted, no response to probes; locked behind crypto challenge. | 🟠 **Medium** |
| **BLE** | HID Keyboard | ✅ User must accept pairing | Full media control via HID Consumer Page. | 🟡 **Low** |

---

## 🚀 Quick Start — ESP32 AVRCP Console

The ESP32 firmware implements a live, interactive console for testing AVRCP injection:

```bash
cd firmware/esp32_avrcp_console
idf.py set-target esp32
idf.py menuconfig   # enable Classic BT + BT L2CAP
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Console Commands

```
avrcp> connect              # Establish Bluetooth connection to earbuds
avrcp> up 5                 # Send 5 volume-up commands
avrcp> down 10              # Send 10 volume-down commands
avrcp> exit                 # Abort current batch (like Ctrl+C)
avrcp> disconnect           # Hang up the ACL gracefully
avrcp> reboot               # Force restart ESP32 (like Ctrl+\)
avrcp> help                 # Show all commands
```

---

## 📂 Repository Structure

```
Bluetooth-Jieli-Research/
├── firmware/
│   └── esp32_avrcp_console/           # ✅ ESP-IDF v6.1 AVRCP injection console (C)
│       ├── main.c                     # Interactive command loop
│       ├── CMakeLists.txt             # ESP-IDF configuration
│       ├── main/CMakeLists.txt        # Component registration
│       ├── sdkconfig.defaults         # Bluetooth configuration
│       ├── .gitignore                 # Excludes build/ & sdkconfig
│       └── README.md                  # Build & deployment guide
│
├── scripts/                           # ✅ Python PoC & analysis tools (Python)
│   ├── avrcp/                         # AVRCP injection attacks (PSM 23)
│   │   ├── avrcp_pause.py             # Volume up/down oscillator
│   │   ├── avrcp_browsing.py          # AVRCP browsing channel probe (PSM 27)
│   │   └── hid_media_keys.py          # HID media key injection
│   │
│   ├── jl_spp/                        # JL-SPP protocol (RFCOMM 1 & 10)
│   │   ├── jl_spp_opcode_scan.py      # Full 256-opcode enumeration
│   │   ├── jl_timing_analysis.py      # Timing side-channel analysis
│   │   ├── jl_prng_period.py          # PRNG period detection
│   │   ├── jl_response_analyzer.py    # Pattern analysis
│   │   ├── jl_prng_pattern.py         # PRNG state observation
│   │   ├── jl_ch10_*.py               # Channel 10 fuzzing attempts
│   │   └── jl_spp_probe.py            # Generic probing
│   │
│   ├── ble/                           # BLE discovery & attacks
│   │   ├── ble_enum.py                # GATT service enumeration
│   │   ├── ble_auth_test.py           # GATT write without auth
│   │   ├── ble_fuzz_notify.py         # Notification fuzzing
│   │   ├── ble_read_classic_mac.py    # MAC address extraction
│   │   └── jieli_hybrid_scanner_attack.py  # Multi-stage attack
│   │
│   ├── proximity/                     # RSSI-based tracking
│   │   └── ble_proximity_monitor.py   # Real-time distance monitoring
│   │
│   ├── utils/                         # Utility functions
│   │   └── HCI_Snooping.py            # Raw HCI socket capture
│   │
│   └── README.md                      # Script inventory & usage
│
├── docs/                              # Technical documentation
│   ├── REPOSITORY_AUDIT.md            # Full repository audit (8.7/10 score)
│   ├── avrcp-technical-reference.md   # AVRCP frame specification
│   └── ...
│
├── data/                              # Experimental results
│   ├── results_opcode_full.txt        # Complete 256-opcode hex dump
│   ├── results_opcode_scan.txt        # Opcode scan with timing
│   ├── results_avrcp.txt              # AVRCP injection results
│   ├── results_ch10_auth.txt          # Channel 10 auth attempts
│   ├── results_static.txt             # Static opcode analysis
│   ├── results_reset.txt              # PRNG reset observations
│   └── README.md                      # Data file descriptions
│
├── paper/                             # Research publication
│   └── draft/Research paper.txt       # Full paper (~2000+ lines)
│
├── logs/                              # Runtime logs (empty, for testing)
├── assets/                            # Images & diagrams
├── tools/                             # Build artifacts
│
├── Root Configuration Files:
│   ├── README.md                       # ✅ This file — main entry point
│   ├── setup.py                        # ✅ Python package configuration
│   ├── requirements.txt                # ✅ Runtime dependencies (pybluez)
│   ├── Makefile                        # ✅ Build automation (make install, make lint, etc.)
│   ├── .gitignore                      # ✅ Git exclusions (build/, *.pyc, etc.)
│   ├── .github/workflows/lint-test.yml # ✅ GitHub Actions CI/CD (auto-lint on push)
│   ├── CONTRIBUTING.md                 # Contributing guidelines
│   ├── ATTACK_VECTORS.md               # Attack vectors summary
│   ├── CHANGELOG.md                    # Version history
│   └── LICENSE                         # MIT License
```

---

## ⚠�� Disclaimer

**This repository is intended solely for security research and educational purposes.**

- All findings were obtained by testing **only on devices owned by the researchers**
- No third-party devices were targeted without consent
- No user data was intercepted or recorded
- This research was conducted to **improve security**, not to enable attacks
- Unauthorized access to devices you do not own is **illegal** in most jurisdictions

**For responsible researchers:** Use this knowledge to audit your own devices, contribute to security improvements, and participate in coordinated disclosure.

---

## 📖 Citation

If you use this work in your research, please cite:

```bibtex
@misc{bluetooth-jieli-research,
  author       = {Sauron, S.S.},
  title        = {Unauthenticated Protocol Exposure and PRNG Weakness in Jieli-Based Bluetooth Audio Devices},
  year         = {2026},
  howpublished = {\url{https://github.com/SS-Sauron/Bluetooth-Jieli-Research}},
}
```

---

## 📄 License

MIT. See `LICENSE` file for details.

---

## 🤝 Contributing

Contributions are welcome! To contribute:

1. **Open an issue** to discuss your idea (before submitting code)
2. **Follow the existing code style** (Python: PEP8, C: ESP-IDF conventions)
3. **Include clear commit messages** and documentation
4. **Update README** if adding new features or scripts
5. **Respect the ethical use guidelines** — security research only

See `CONTRIBUTING.md` for detailed guidelines.

---

## 🙏 Acknowledgments

This research builds upon prior work in Bluetooth security:
- **BlueBorne** (Armis, 2017)
- **BrakTooth** (2021)
- **Airoha RACE** (2023)

We thank the open-source community for PyBluez, BlueZ, and ESP-IDF.

---

## ❓ FAQ

**Q: Can I use this to attack any Jieli device?**  
A: These vulnerabilities apply to Jieli-based devices in general, though specifics may vary by firmware version. Test on devices you own.

**Q: Why doesn't this work on my earbuds?**  
A: Different Jieli firmwares may have different opcodes or require different device states. See `TROUBLESHOOTING.md` (or open an issue).

---

## 📞 Contact & Reporting

For security vulnerabilities or questions:
- **GitHub Issues:** https://github.com/SS-Sauron/Bluetooth-Jieli-Research/issues
- **Research inquiries:** [Contact the author]
- **Responsible disclosure:** Follow coordinated vulnerability disclosure practices

---

**Last Updated:** May 2026  
**Status:** 🟡 Active Research  
**Maintenance:** Community-driven

# 🔐 Bluetooth Jieli Research

[![Category: Security Research](https://img.shields.io/badge/Category-Security_Research-blue)](https://github.com/SS-Sauron/Bluetooth-Jieli-Research)
[![Disclosure: In Progress](https://img.shields.io/badge/Disclosure-In_Progress-yellow)](#responsible-use-and-disclosure)
[![Tested Target: Soundcore R50i NC](https://img.shields.io/badge/Tested_Target-Soundcore_R50i_NC-lightgrey)](#research-scope)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.1_local%20%7C%20v6.0_CI-green)](#compatibility)

Security research on unauthenticated Bluetooth Classic behavior observed on a
Soundcore R50i NC headset using a Jieli chipset and firmware v01.65.

## 📌 Status

| Field | Value |
| :--- | :--- |
| Target tested | Soundcore R50i NC |
| Firmware tested | v01.65 |
| Chipset family | Jieli |
| Methodology | Black-box protocol testing with commodity Bluetooth hardware |
| Protocols tested | Bluetooth Classic BR/EDR, limited BLE discovery and HID checks |
| Disclosure | Coordinated disclosure in progress |
| Claim boundary | Confirmed findings apply to the tested target and firmware only |

## ⚠️ Responsible Use and Disclosure

This repository is for authorized security research on devices you own or have
explicit permission to test. The proof-of-concept scripts can transmit Bluetooth
control traffic and may change the state of nearby devices if misused. Do not
run them against third-party equipment.

The research is currently in coordinated disclosure. Public claims in this
README are intentionally limited to observations reproduced on the tested
Soundcore R50i NC firmware version. Similar Jieli-based devices are useful
future validation targets, but they are not claimed as affected unless tested.

## 📚 Table of Contents

- [Executive Summary](#executive-summary)
- [Research Scope](#research-scope)
- [Finding Taxonomy](#finding-taxonomy)
- [Confirmed Security Findings](#confirmed-security-findings)
- [Research Observations](#research-observations)
- [Proof-of-Concept Reproduction](#proof-of-concept-reproduction)
- [Evidence and Data](#evidence-and-data)
- [Compatibility](#compatibility)
- [Repository Map](#repository-map)
- [Citation](#citation)
- [Contributing](#contributing)
- [Contact and Reporting](#contact-and-reporting)

## 🎯 Executive Summary

This project evaluates Bluetooth protocol exposure on the Soundcore R50i NC
running firmware v01.65. Testing found that the device accepts selected
Bluetooth Classic control connections from an unpaired host within radio range.
The highest-impact confirmed behavior is unauthenticated AVRCP volume control
over L2CAP PSM 23 and unauthenticated access to a proprietary JL-SPP service on
RFCOMM channel 1.

The repository contains Python proof-of-concept scripts, ESP32 firmware, and
captured result files that document the observed behavior. The scripts are
operator-focused so another researcher can reproduce the tests on owned
hardware, but the security claims remain scoped to the single tested device and
firmware.

## 🔬 Research Scope

| Item | In scope |
| :--- | :--- |
| Device | Soundcore R50i NC |
| Firmware | v01.65 |
| Primary protocol surface | Bluetooth Classic BR/EDR |
| Classic interfaces tested | AVRCP over L2CAP PSM 23, AVRCP browsing probe on PSM 27, JL-SPP over RFCOMM |
| BLE testing | Discovery, GATT probing, media-key/HID experiments |
| Out of scope | Vendor source review, firmware extraction, attacks against third-party devices |

This work should not be read as a universal statement about all Jieli-based
products. It is evidence that the tested device exposes security-sensitive
interfaces without the authentication expected by a user who has paired the
headset only with their own phone.

## 🧭 Finding Taxonomy

The project separates security findings from supporting observations:

- Confirmed security findings: behavior with a direct security impact on the
  tested target.
- Research observations: protocol behavior that supports analysis but is not by
  itself a vulnerability.
- Tooling and protocol notes: scripts, firmware, packet layouts, and workflow
  details used to reproduce the research.
- Negative results: experiments that did not produce the attempted bypass.

## 🛡️ Confirmed Security Findings

| ID | Finding | Protocol | Preconditions | Impact | Evidence | CWE |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| F-01 | Unauthenticated AVRCP control connection accepts selected PASSTHROUGH commands on L2CAP PSM 23 | Classic AVRCP | Attacker-controlled Bluetooth Classic host within range; target powered on | Unpaired host can affect media-control behavior observed on the headset/phone path, including volume changes on the tested setup | `scripts/avrcp/avrcp_pause.py`, `data/results_avrcp.txt`, `docs/avrcp-technical-reference.md` | CWE-306 |
| F-02 | Split trust boundary between transport controls and volume controls | Classic AVRCP | Same as F-01; phone connected during playback | Volume commands were accepted where other media commands were filtered or behaved differently, indicating inconsistent authorization across control classes | `data/results_avrcp.txt`, `firmware/esp32_avrcp_console/` | CWE-306 |
| F-03 | Proprietary JL-SPP service accepts unauthenticated RFCOMM connections | Classic RFCOMM/JL-SPP | Bluetooth Classic host within range; target address known or discovered | RFCOMM channels 1 and 10 accepted connections without pairing; channel 1 returned structured responses to probes | `scripts/jl_spp/channel_scanner`, `scripts/jl_spp/jl_spp_opcode_scan.py`, `data/results_opcode_full.txt` | CWE-306 |
| F-04 | JL-SPP channel 1 exposes opcode-dependent responses | Classic RFCOMM/JL-SPP | Successful channel 1 connection | The device responded to all 256 tested opcode values, creating an unauthenticated protocol surface for fingerprinting and further analysis | `scripts/jl_spp/jl_spp_opcode_scan.py`, `data/results_opcode_scan.txt`, `data/results_opcode_full.txt` | CWE-200 |
| F-05 | JL-SPP response timing varies by opcode class | Classic RFCOMM/JL-SPP | Successful channel 1 connection; repeated opcode probes | Response latency clusters can reveal protocol state or handler differences without authentication | `scripts/jl_spp/jl_timing_analysis.py`, `scripts/jl_spp/jl_full_timing_scan.py` | CWE-208 |
| F-06 | JL-SPP response stream shows weak pseudo-random behavior and reset patterns | Classic RFCOMM/JL-SPP | Successful channel 1 connection; repeated probes | Generated values showed repeatable structure and reset behavior during testing, reducing confidence that the channel uses cryptographic randomness | `scripts/jl_spp/jl_prng_period.py`, `scripts/jl_spp/jl_reset_pattern.py`, `data/results_reset.txt` | CWE-338 |

## 🔎 Research Observations

| ID | Observation | Category | Evidence | Notes |
| :--- | :--- | :--- | :--- | :--- |
| O-01 | AVRCP PASSTHROUGH command format was confirmed as an 8-byte AV/C frame over AVCTP in the tested path | Protocol note | `docs/avrcp-technical-reference.md` | Supports reproduction of F-01 and F-02 |
| O-02 | Sending a UNIT INFO request caused the tested device to close the L2CAP connection | Protocol behavior | `scripts/avrcp/avrcp_pause.py` | Useful for state-machine analysis; not classified as a standalone vulnerability |
| O-03 | JL-SPP RFCOMM channel 10 accepted a connection but did not respond to simple probes | Negative result | `data/results_ch10_auth.txt`, `data/results_ch10_fuzz.txt` | Treated as an exposed but not fully characterized surface |
| O-04 | MAC spoofing attempts against an actively connected phone did not bypass baseband behavior | Negative result | `ATTACK_VECTORS.md`, `firmware/esp32_avrcp_console/` | Included to document a tested path that did not produce the intended bypass |
| O-05 | BLE HID media-key control requires pairing/user acceptance | Adjacent BLE behavior | `scripts/ble/ble_media_keys.py`, `data/results_hid.txt` | Not an unauthenticated finding; kept as context for media-control trust boundaries |

## ⚙️ Proof-of-Concept Reproduction

Run the following only against devices you own or are authorized to test. The
default scripts include addresses from the test device used in this research;
edit the target constants before reproducing on your own hardware.

### 🐍 Python Environment

Use Python 3.8 through 3.11 for the default dependency path. `pybluez==0.23` is
pinned in `requirements.txt` and exposes the `bluetooth` Python module used by
the Classic Bluetooth scripts.

```bash
git clone https://github.com/SS-Sauron/Bluetooth-Jieli-Research.git
cd Bluetooth-Jieli-Research

python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

If `pybluez` fails to build on your platform, use `pybluez2` as a drop-in
provider for the same `bluetooth` module:

```bash
python -m pip install pybluez2 "bleak>=0.21.0"
```

Verify the imports used by the scripts:

```bash
python -c "import bluetooth; print('bluetooth module OK')"
python -c "import bleak; print('bleak OK')"
```

### 📡 Classic Bluetooth Prerequisites

On Linux, install BlueZ development headers before installing the Python
dependencies:

```bash
sudo apt update
sudo apt install -y bluetooth bluez libbluetooth-dev
```

Confirm the local adapter is present and powered:

```bash
bluetoothctl show
bluetoothctl devices
```

Some raw Bluetooth operations may require elevated privileges or local adapter
configuration. Record the adapter model, BlueZ version, target firmware, and
target state when collecting reproducibility data.

### 🔌 JL-SPP Channel Discovery

The channel scanner tests RFCOMM channels 1 through 15 and reports whether the
target accepts a connection.

```bash
python scripts/jl_spp/channel_scanner
```

Expected result on the tested firmware: channels 1 and 10 accepted RFCOMM
connections without prior pairing. Channel 1 was then used for opcode-response
mapping:

```bash
python scripts/jl_spp/jl_spp_opcode_scan.py
python scripts/jl_spp/jl_timing_analysis.py
python scripts/jl_spp/jl_prng_period.py
```

Expected artifacts: console output comparable to `data/results_opcode_scan.txt`,
`data/results_opcode_full.txt`, and `data/results_reset.txt`.

### 🎚️ AVRCP Control Testing

The AVRCP script opens L2CAP PSM 23 and sends a media-control test sequence.
Observe the phone and headset state while the script runs.

```bash
python scripts/avrcp/avrcp_pause.py
```

Expected result on the tested setup: the target accepted the unauthenticated
connection and selected PASSTHROUGH commands produced observable behavior,
including volume changes. Some transport controls may be filtered by the phone,
the headset, or the current playback state.

### 📶 BLE Discovery and Adjacent Checks

BLE scripts are included for discovery and adjacent media-control experiments.
They are not the primary unauthenticated Classic Bluetooth findings.

```bash
python scripts/ble/ble_enum.py
python scripts/ble/ble_read_classic_mac.py
```

BLE HID media-key tests require pairing or user approval and should be treated
as a different trust model from the unauthenticated Classic findings.

### 🧩 ESP32 AVRCP Console

The ESP32 firmware provides an interactive AVRCP test console for owned-device
reproduction.

Local development instructions target ESP-IDF v6.1. The current GitHub Actions
workflow builds firmware with the `espressif/idf:release-v6.0` container, so
both versions are represented in the project.

```bash
git clone --branch v6.1 https://github.com/espressif/esp-idf.git ~/esp-idf
cd ~/esp-idf
./install.sh
source ./export.sh

cd /path/to/Bluetooth-Jieli-Research/firmware/esp32_avrcp_console
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Console commands:

```text
avrcp> connect [MAC]
avrcp> up 5
avrcp> down 10
avrcp> exit
avrcp> disconnect
avrcp> reboot
avrcp> help
```

See `firmware/esp32_avrcp_console/README.md` for firmware-specific notes.

## 📁 Evidence and Data

| Area | Files |
| :--- | :--- |
| AVRCP frame format and protocol notes | `docs/avrcp-technical-reference.md` |
| Script inventory | `scripts/README.md` |
| Processed experiment outputs | `data/README.md`, `data/results_*.txt` |
| JL-SPP opcode mapping | `data/results_opcode_full.txt`, `data/results_opcode_scan.txt` |
| PRNG/reset behavior | `data/results_reset.txt`, `data/results_challenge*.txt` |
| Channel 10 experiments | `data/results_ch10_auth.txt`, `data/results_ch10_fuzz.txt` |
| ESP32 firmware | `firmware/esp32_avrcp_console/` |
| Broader attack notes | `ATTACK_VECTORS.md` |

When adding new evidence, include the target device, firmware version, Bluetooth
adapter, host OS, script revision, and exact device state.

## 🧪 Compatibility

| Component | Tested or expected value |
| :--- | :--- |
| Target device | Soundcore R50i NC |
| Target firmware | v01.65 |
| Host OS | Ubuntu 22.04; macOS and WSL2 are partially documented but less complete for Classic Bluetooth |
| Bluetooth stack | BlueZ 5.x for Linux Classic Bluetooth testing |
| USB adapters | CSR8510 A10, Broadcom BCM20702A0, or comparable Bluetooth 4.0+ adapter |
| Python | 3.8-3.11 recommended for `pybluez==0.23` |
| Classic Bluetooth Python module | `pybluez` or `pybluez2`, imported as `bluetooth` |
| BLE Python library | `bleak>=0.21.0` |
| ESP-IDF | v6.1 for local setup; v6.0 in current CI firmware build |

## 🗂️ Repository Map

```text
Bluetooth-Jieli-Research/
|-- README.md                         # Main research entry point
|-- ATTACK_VECTORS.md                 # Additional experiment notes
|-- CHANGELOG.md                      # Project history
|-- CONTRIBUTING.md                   # Contribution guidelines
|-- requirements.txt                  # Runtime Python dependencies
|-- setup.py                          # Python package metadata
|-- data/                             # Processed experiment outputs
|-- docs/                             # Technical protocol documentation
|-- firmware/esp32_avrcp_console/     # ESP32 AVRCP test console
|-- scripts/                          # Python PoC and analysis scripts
|-- tests/                            # Basic repository tests
`-- tools/                            # Tooling notes
```

## 📖 Citation

If you use this repository in research, cite it as:

```bibtex
@misc{bluetooth-jieli-research,
  author       = {Sauron, S.S.},
  title        = {Unauthenticated Protocol Exposure and PRNG Weakness in a Jieli-Based Bluetooth Audio Device},
  year         = {2026},
  howpublished = {\url{https://github.com/SS-Sauron/Bluetooth-Jieli-Research}},
  note         = {Security research repository, coordinated disclosure in progress},
}
```

## 🤝 Contributing

Contributions are welcome when they improve reproducibility, documentation, or
defensive analysis. Open an issue before submitting substantial changes, include
the tested device and firmware version for new findings, and keep all testing
within authorized devices.

See `CONTRIBUTING.md` for the project contribution guidelines.

## 📄 License

Project metadata declares the license as MIT. Add a repository-level license
file before publishing a formal release or advisory.

## 🙏 Acknowledgments

This work builds on prior Bluetooth security research, including BlueBorne,
BrakTooth, and public analysis of vendor-specific Bluetooth control protocols.
Thanks to the maintainers of BlueZ, PyBluez, Bleak, and ESP-IDF.

## 📬 Contact and Reporting

- GitHub issues: https://github.com/SS-Sauron/Bluetooth-Jieli-Research/issues
- Security or disclosure questions: use coordinated vulnerability disclosure
  practices and avoid publishing third-party device identifiers.
- Research inquiries: open an issue with device model, firmware version, host
  environment, and the specific script or evidence file involved.

- Last updated: May 2026
- Research status: 🟡 Active, coordinated disclosure in progress
- Maintenance: Community-driven

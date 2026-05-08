```text
+------------------------------------------------------------------------------------------+
|                          BLUETOOTH JIELI RESEARCH                                        |
|          Classic BR/EDR | JL-SPP | AVRCP | ESP32 | owned-device lab                      |
+------------------------------------------------------------------------------------------+
```

# 🔐 Bluetooth Jieli Research

[![Category: Security Research](https://img.shields.io/badge/Category-Security_Research-blue)](https://github.com/SS-Sauron/Bluetooth-Jieli-Research)
[![Disclosure: In Progress](https://img.shields.io/badge/Disclosure-In_Progress-yellow)](#responsible-use-and-disclosure)
[![Tested Target: Soundcore R50i NC](https://img.shields.io/badge/Tested_Target-Soundcore_R50i_NC-lightgrey)](#research-scope)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.1_local%20%7C%20v6.0_CI-green)](#compatibility)
[![PoC: Python + ESP32](https://img.shields.io/badge/PoC-Python%20%2B%20ESP32-purple)](#proof-of-concept-reproduction)
[![Scope: Single Firmware](https://img.shields.io/badge/Scope-Soundcore_R50i_NC_v01.65-orange)](#research-scope)

> 🕶️ **Field note:** this project lives in the space between consumer earbuds,
> commodity radios, and the debug interfaces that should not be reachable from
> the street.

---

## Why This Matters

Bluetooth audio gear usually looks quiet from the outside: pair it, play music,
forget it exists. This repository is the opposite view: a low-level field lab
for poking at the **Classic Bluetooth control paths**, **vendor SPP channels**,
**timing behavior**, and **trust boundaries** exposed by one Jieli-based headset.

The confirmed target is the **Soundcore R50i NC** on **firmware v01.65**. The
tone is practical and hands-on, but the claim boundary stays tight: this is
evidence from owned-device testing, not a blanket statement about every Jieli
product.

- 🎧 **Target:** Soundcore R50i NC, firmware v01.65
- 📡 **Surface:** Bluetooth Classic BR/EDR, with limited BLE side quests
- 🔌 **Main finds:** unauthenticated AVRCP behavior and exposed JL-SPP service paths
- 🧪 **Proof:** Python PoCs, ESP32 firmware, and captured result files
- 🟡 **Disclosure:** coordinated disclosure in progress

| Field | Value |
| :--- | :--- |
| Target tested | 🎧 Soundcore R50i NC |
| Firmware tested | v01.65 |
| Chipset family | Jieli |
| Methodology | 🧰 Black-box protocol testing with commodity Bluetooth hardware |
| Protocols tested | 📡 Bluetooth Classic BR/EDR, limited BLE discovery and HID checks |
| Disclosure | 🟡 Coordinated disclosure in progress |
| Claim boundary | ✅ Confirmed findings apply to the tested target and firmware only |

> ⚠️ **Ethical boundary:** this is a research bench, not a drive-by toolkit.
> Use it only on devices you own or have explicit permission to test.

---

## Attack Surface Flow

<pre>
🧑‍💻 Research Host
      │
      ├── 🔴 Classic BR/EDR
      │       ├── L2CAP PSM 23  ──► AVRCP control path ──► 🎧 Soundcore R50i NC
      │       └── RFCOMM 1/10   ──► JL-SPP surface      ──► 🔌 vendor protocol logic
      │
      ├── 🟡 BLE checks
      │       └── Discovery / GATT / HID experiments
      │
      └── 🧩 ESP32 console path
              └── AVRCP test firmware ────────────────► 🎚️ volume/control behavior

🎧 Soundcore R50i NC ── observed media-control effects ──► 📱 paired phone path
</pre>

---

## Table of Contents

- [🎯 Executive Summary](#executive-summary)
- [🔬 Research Scope](#research-scope)
- [🧭 Finding Taxonomy](#finding-taxonomy)
- [🚨 Confirmed Security Findings](#confirmed-security-findings)
- [🔎 Research Observations](#research-observations)
- [⚡ Proof-of-Concept Reproduction](#proof-of-concept-reproduction)
- [📁 Evidence and Data](#evidence-and-data)
- [🧪 Compatibility](#compatibility)
- [🗂️ Repository Map](#repository-map)
- [⚠️ Responsible Use and Disclosure](#responsible-use-and-disclosure)
- [📖 Citation](#citation)
- [🤝 Contributing](#contributing)
- [📄 License](#license)
- [📬 Contact and Reporting](#contact-and-reporting)

---

## Executive Summary

This project evaluates Bluetooth protocol exposure on the Soundcore R50i NC
running firmware v01.65. The short version: the headset trusted more radio-side
traffic than a normal user would expect from an unpaired host. Testing found
selected Bluetooth Classic control paths reachable from within radio range,
including AVRCP behavior over L2CAP PSM 23 and a proprietary JL-SPP service on
RFCOMM channel 1.

The repository contains Python proof-of-concept scripts, ESP32 firmware, and
captured result files that document the observed behavior. The scripts are
operator-focused so another researcher can reproduce the tests on owned
hardware, but the security claims remain scoped to the single tested device and
firmware.

**What makes this interesting**

- 🎚️ Volume/control behavior crossed a trust boundary that should be boring.
- 🔌 A vendor-specific RFCOMM surface accepted unauthenticated connections.
- ⏱️ Opcode timing and response patterns gave the hidden protocol a shape.
- 🧱 Some attempted paths failed, and those negative results are documented too.

---

## Research Scope

| Item | In scope |
| :--- | :--- |
| Device | Soundcore R50i NC |
| Firmware | v01.65 |
| Primary protocol surface | Bluetooth Classic BR/EDR |
| Classic interfaces tested | AVRCP over L2CAP PSM 23, AVRCP browsing probe on PSM 27, JL-SPP over RFCOMM |
| BLE testing | Discovery, GATT probing, media-key/HID experiments |
| Out of scope | Vendor source review, firmware extraction, attacks against third-party devices |

This work should not be read as a universal statement about all Jieli-based
products. It is a clean signal from one tested device: security-sensitive
interfaces were reachable without the authentication a user would expect after
pairing the headset only with their own phone.

---

## Finding Taxonomy

The project separates the sharp edges from the lab dust:

- 🛡️ Confirmed security findings: behavior with a direct security impact on the
  tested target.
- 🔎 Research observations: protocol behavior that supports analysis but is not by
  itself a vulnerability.
- 🧰 Tooling and protocol notes: scripts, firmware, packet layouts, and workflow
  details used to reproduce the research.
- 🧱 Negative results: experiments that did not produce the attempted bypass.

---

## Confirmed Security Findings

These are the sharp findings reproduced on the tested Soundcore R50i NC
firmware. Severity reflects the local research impact on this target, not a
vendor-wide rating.

> 🚨 **Impact snapshot**
>
> - 🔴 Critical: unauthenticated proprietary JL-SPP surface exposed over RFCOMM.
> - 🟠 High: unpaired Classic Bluetooth host can reach AVRCP control behavior.
> - 🟠 High: volume and transport controls do not share a clean trust boundary.
> - 🟡 Medium: opcode responses and timing leak enough shape to map hidden logic.

| ID | Severity | Finding | CWE |
| :--- | :--- | :--- | :--- |
| F-01 | 🟠 High | Unauthenticated AVRCP control path on L2CAP PSM 23. | CWE-306 |
| F-02 | 🟠 High | Split trust boundary for volume control. | CWE-306 |
| F-03 | 🔴 Critical | Exposed proprietary JL-SPP service over RFCOMM. | CWE-306 |
| F-04 | 🟠 High | Full opcode response surface on JL-SPP channel 1. | CWE-200 |
| F-05 | 🟡 Medium | Timing side channel across JL-SPP opcodes. | CWE-208 |
| F-06 | 🔴 Critical | Weak randomness and reset patterns in JL-SPP responses. | CWE-338 |

**Finding ledger**

- **F-01:** Selected PASSTHROUGH commands were accepted from an unpaired host.
  Preconditions: Bluetooth Classic host within range; target powered on.
  Evidence: `scripts/avrcp/avrcp_pause.py`, `data/results_avrcp.txt`,
  `docs/avrcp-technical-reference.md`.
- **F-02:** Volume commands were accepted where other media controls were
  filtered or behaved differently. Preconditions: same setup as F-01; phone
  connected during playback. Evidence: `data/results_avrcp.txt`,
  `firmware/esp32_avrcp_console/`.
- **F-03:** RFCOMM channels 1 and 10 accepted connections without pairing;
  channel 1 returned structured responses. Preconditions: Bluetooth Classic
  host within range; target address known or discovered. Evidence:
  `scripts/jl_spp/channel_scanner`, `scripts/jl_spp/jl_spp_opcode_scan.py`,
  `data/results_opcode_full.txt`.
- **F-04:** All 256 tested opcode values produced device responses.
  Preconditions: successful unauthenticated RFCOMM channel 1 connection.
  Evidence: `scripts/jl_spp/jl_spp_opcode_scan.py`,
  `data/results_opcode_scan.txt`, `data/results_opcode_full.txt`.
- **F-05:** Response latency varied by opcode class during repeated probes.
  Preconditions: successful channel 1 connection; repeated opcode probes.
  Evidence: `scripts/jl_spp/jl_timing_analysis.py`,
  `scripts/jl_spp/jl_full_timing_scan.py`.
- **F-06:** JL-SPP response data showed repeatable structure and reset behavior
  during testing. Preconditions: successful channel 1 connection; repeated
  probes. Evidence: `scripts/jl_spp/jl_prng_period.py`,
  `scripts/jl_spp/jl_reset_pattern.py`, `data/results_reset.txt`.

---

## Research Observations

These are not filler. They are the edges of the map: protocol primitives,
negative results, and trust-boundary clues that keep the confirmed findings
honest.

| ID | Observation | Evidence |
| :--- | :--- | :--- |
| O-01 | AVRCP PASSTHROUGH frame shape confirmed. | `docs/avrcp-technical-reference.md` |
| O-02 | UNIT INFO closes the L2CAP session. | `scripts/avrcp/avrcp_pause.py` |
| O-03 | JL-SPP channel 10 is exposed but quiet. | `data/results_ch10_auth.txt`, `data/results_ch10_fuzz.txt` |
| O-04 | MAC spoofing did not bypass active baseband behavior. | `ATTACK_VECTORS.md`, `firmware/esp32_avrcp_console/` |
| O-05 | BLE HID media keys require pairing. | `scripts/ble/ble_media_keys.py`, `data/results_hid.txt` |

```markdown
# 🧬 AVRCP Packet Breakdown

The 8‑byte AVCTP frame used for PASSTHROUGH commands (values are an example):
+---------+-------+-------+-------+-------+-------+-------+-------+
| Byte 0  | B1    | B2    | B3    | B4    | B5    | B6    | B7    |
+---------+-------+-------+-------+-------+-------+-------+-------+
| AVCTP   | PID-H | PID-L | ctype | Sub   | Op    | Data  | State |
| (0x00)  | (11)  | (0E)  | (00)  | (48)  | (7C)  | (41)  | (00)  |
+---------+-------+-------+-------+-------+-------+-------+-------+
```

**Observation ledger**

- **O-01:** The tested path used an 8-byte AV/C frame over AVCTP for AVRCP
  PASSTHROUGH commands. This is the packet primitive behind F-01 and F-02.
- **O-02:** Sending a UNIT INFO request caused the tested device to close the
  L2CAP connection, giving the AVRCP state machine a visible edge for follow-up
  analysis.
- **O-03:** RFCOMM channel 10 accepted a connection but did not respond to simple
  probes. It remains an unauthenticated exposed surface, even if the simple
  probe set did not unlock useful behavior.
- **O-04:** Spoofing attempts against an actively connected phone did not produce
  the intended bypass. Negative results matter; this documents a path that did
  not work under the tested conditions.
- **O-05:** BLE HID media-key control requires pairing or user approval. This is
  a different trust model from the unauthenticated Classic Bluetooth findings.

---

## Proof-of-Concept Reproduction

> ⚡ **Operator note:** run the following only against devices you own or are
> authorized to test. The default scripts include addresses from the test device
> used in this research; edit the target constants before reproducing on your own
> hardware.

> 🧭 **Lab checklist**
>
> - Record the target firmware, host OS, adapter, distance, and device state.
> - Run probes more than once; Bluetooth state machines can be moody.
> - Stop if the device is not yours, not in your lab, or not explicitly in scope.

### Python Environment

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

> 🛠️ **Troubleshooting:** if setup gets noisy, check `TROUBLESHOOTING.md`
> before changing the scripts.

### Classic Bluetooth Prerequisites

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

### JL-SPP Channel Discovery

The channel scanner tests RFCOMM channels 1 through 15 and reports whether the
target accepts a connection.

```bash
python scripts/jl_spp/channel_scanner
```

Expected result on the tested firmware: channels 1 and 10 accepted RFCOMM
connections without prior pairing. Channel 1 is where the tunnel starts to light
up, so it was used for opcode-response mapping:

```bash
python scripts/jl_spp/jl_spp_opcode_scan.py
python scripts/jl_spp/jl_timing_analysis.py
python scripts/jl_spp/jl_prng_period.py
```

Expected artifacts: console output comparable to `data/results_opcode_scan.txt`,
`data/results_opcode_full.txt`, and `data/results_reset.txt`.

### AVRCP Control Testing

The AVRCP script opens L2CAP PSM 23 and sends a media-control test sequence.
Observe the phone and headset state while the script runs.

```bash
python scripts/avrcp/avrcp_pause.py
```

Expected result on the tested setup: the target accepted the unauthenticated
connection and selected PASSTHROUGH commands produced observable behavior,
including volume changes. Some transport controls may be filtered by the phone,
the headset, or the current playback state, so take notes like you are watching
a state machine through a keyhole.

### BLE Discovery and Adjacent Checks

BLE scripts are included for discovery and adjacent media-control experiments.
They are not the primary unauthenticated Classic Bluetooth findings.

```bash
python scripts/ble/ble_enum.py
python scripts/ble/ble_read_classic_mac.py
```

BLE HID media-key tests require pairing or user approval and should be treated
as a different trust model from the unauthenticated Classic findings.

### ESP32 AVRCP Console

The ESP32 firmware provides an interactive AVRCP test console for owned-device
reproduction. Think of it as the pocket-sized hardware path for the same control
surface tested from Python.

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

---

## Evidence and Data

The repo is built around artifacts, not vibes. If a claim matters, it should
point back to a script, a result file, or a protocol note.

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
| Setup and runtime troubleshooting | `TROUBLESHOOTING.md` |

When adding new evidence, include the target device, firmware version, Bluetooth
adapter, host OS, script revision, and exact device state. Good Bluetooth
research is half packet work and half lab notebook.

---

## Compatibility

This project is closest to the metal on Linux with BlueZ. Other platforms may
work for BLE or high-level checks, but Classic Bluetooth PoC behavior depends on
the local stack and adapter.

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

---

## Repository Map

The layout is intentionally small: scripts for live work, data for receipts,
docs for protocol notes, and firmware for the ESP32 path.

```text
Bluetooth-Jieli-Research/
|-- README.md                         # Main research entry point
|-- ATTACK_VECTORS.md                 # Additional experiment notes
|-- CHANGELOG.md                      # Project history
|-- CONTRIBUTING.md                   # Contribution guidelines
|-- TROUBLESHOOTING.md                # Common setup and runtime fixes
|-- requirements.txt                  # Runtime Python dependencies
|-- setup.py                          # Python package metadata
|-- data/                             # Processed experiment outputs
|-- docs/                             # Technical protocol documentation
|-- firmware/esp32_avrcp_console/     # ESP32 AVRCP test console
|-- scripts/                          # Python PoC and analysis scripts
|-- tests/                            # Basic repository tests
`-- tools/                            # Tooling notes
```

---

## Responsible Use and Disclosure

> ⚠️ **Authorized research only**
>
> This is a research bench, not a drive-by toolkit. Use it only on devices you own
> or have explicit permission to test. The proof-of-concept scripts can transmit
> Bluetooth control traffic and may change the state of nearby devices if misused.
> Do not run them against third-party equipment.

> 🟡 **Disclosure status**
>
> The research is currently in coordinated disclosure. Public claims in this
> README are intentionally limited to observations reproduced on the tested
> Soundcore R50i NC firmware version. Similar Jieli-based devices are useful
> future validation targets, but they are not claimed as affected unless tested.

---

## Citation

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

---

## Contributing

Contributions are welcome when they make the signal cleaner: better
reproduction notes, cleaner captures, defensive analysis, or validation on owned
hardware. Open an issue before submitting substantial changes, include the
tested device and firmware version for new findings, and keep all testing within
authorized devices.

See `CONTRIBUTING.md` for the project contribution guidelines.

---

## License

MIT — see the [LICENSE](./LICENSE) file for full terms.

---

## Acknowledgments

This work builds on prior Bluetooth security research, including BlueBorne,
BrakTooth, and public analysis of vendor-specific Bluetooth control protocols.
Thanks to the maintainers of BlueZ, PyBluez, Bleak, and ESP-IDF.

Also: respect to the open-source researchers who publish the strange details
that vendors usually leave in the dark.

---

## Contact and Reporting

- GitHub issues: https://github.com/SS-Sauron/Bluetooth-Jieli-Research/issues
- Security or disclosure questions: use coordinated vulnerability disclosure
  practices and avoid publishing third-party device identifiers.
- Research inquiries: open an issue with device model, firmware version, host
  environment, and the specific script or evidence file involved.
---
- Last updated: May 2026
- Research status: 🟡 Active, coordinated disclosure in progress
- Maintenance: Community-driven, lab-first, receipts-required

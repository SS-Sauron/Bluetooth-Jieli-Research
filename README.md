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

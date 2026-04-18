# Scripts Inventory

## AVRCP (`avrcp/`)
- `avrcp_pause.py` – Volume up/down oscillator and Play/Pause injection test.
- `avrcp_browsing.py` – Probe AVRCP browsing channel (PSM 27).
- `hid_media_keys.py` – Attempt HID media key injection.

## JL_SPP Protocol (`jl_spp/`)
- `jl_spp_opcode_scan.py` – Full 256-opcode scan on RFCOMM channel 1.
- `jl_full_timing_scan.py` – Timing side-channel analysis.
- `jl_reset_pattern.py` – PRNG reset via opcode 0x47.
- `jl_ch10_*.py` – Channel 10 authentication and fuzzing attempts.

## BLE (`ble/`)
- `ble_read_classic_mac.py` – Enumerate BLE services, attempt MAC read.
- `jieli_hybrid_scanner_attack.py` – BLE discovery → Classic MAC derivation → AVRCP attack.

## Proximity (`proximity/`)
- `ble_proximity_monitor.py` – Real-time RSSI monitor with visual bar and logging.

## Utilities (`utils/`)
- `HCI_Snooping.py` – Raw HCI socket capture for Python.

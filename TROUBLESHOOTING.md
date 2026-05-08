# Troubleshooting

This page covers common setup and runtime problems for this repository. For the
main workflow and scope, see [README.md](README.md).

## `pip install pybluez` Fails

Symptoms may include `use_2to3 is invalid`, missing `btmodule.h`, or build
errors from `pybluez==0.23`.

Use `pybluez2` as the fallback provider for the same `bluetooth` Python module:

```bash
python -m pip uninstall -y pybluez
python -m pip install pybluez2 "bleak>=0.21.0"
python -c "import bluetooth; print('bluetooth module OK')"
```

On Linux, also make sure BlueZ headers are installed:

```bash
sudo apt update
sudo apt install -y bluetooth bluez libbluetooth-dev
```

## Bluetooth Adapter Not Found or Permission Denied

Linux checks:

```bash
bluetoothctl show
rfkill list bluetooth
sudo systemctl status bluetooth
```

If the adapter is blocked or powered off:

```bash
sudo rfkill unblock bluetooth
bluetoothctl power on
```

macOS checks:

- Grant Bluetooth permission to the terminal app in System Settings.
- Restart the terminal after changing privacy permissions.
- Prefer Linux/BlueZ for Classic Bluetooth PoC scripts; macOS support is less
  consistent for raw Classic Bluetooth operations.

## ESP-IDF Build Errors

If `idf.py` is missing, the toolchain is not active in the current shell:

```bash
source ~/esp-idf/export.sh
idf.py --version
```

If the build fails after switching ESP-IDF versions, clean and rebuild:

```bash
cd firmware/esp32_avrcp_console
idf.py fullclean
idf.py set-target esp32
idf.py build
```

This repository documents local ESP-IDF v6.1 usage while CI currently builds
with ESP-IDF v6.0. Use one supported version consistently in a given checkout.

## No Devices Found During Scanning

Check the basics before changing code:

- Confirm the target is powered on, discoverable if needed, and within range.
- Move closer and remove other active Bluetooth connections where possible.
- Confirm the script target address matches your owned device.
- Restart the local adapter if discovery is stale.
- Some device states expose fewer services when already paired, connected, or in
  a low-power case/charging state.

Useful commands:

```bash
bluetoothctl scan on
bluetoothctl devices
```

## Classic Bluetooth Requires Elevated Permissions

Some systems restrict raw L2CAP, RFCOMM, or HCI operations to privileged users.
If a Classic Bluetooth script fails with permission errors, retry only on an
authorized test device with elevated privileges:

```bash
sudo -E python scripts/avrcp/avrcp_pause.py
sudo -E python scripts/jl_spp/channel_scanner
```

Avoid running scripts with elevated privileges against devices you do not own or
do not have explicit permission to test.

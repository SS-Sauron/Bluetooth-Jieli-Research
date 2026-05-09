# PC/Laptop Bluetooth Adapter Setup

This guide is for running the Python proof-of-concept scripts from a PC or
laptop with a Bluetooth adapter. It is separate from the ESP32 firmware setup
because not every researcher will have an ESP32 board.

Linux with BlueZ is the primary supported path for the Classic Bluetooth tests.
Other platforms may work for BLE or high-level checks, but the Classic
Bluetooth behavior depends heavily on the local Bluetooth stack and adapter.

## Linux Quick Start

Use Python 3.8 through 3.11 for the default dependency path. `pybluez==0.23` is
pinned in `requirements.txt` and exposes the `bluetooth` Python module used by
the Classic Bluetooth scripts.

```bash
git clone https://github.com/SS-Sauron/Bluetooth-Jieli-Research.git
cd Bluetooth-Jieli-Research

sudo apt update
sudo apt install -y bluetooth bluez libbluetooth-dev

python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

Verify the local adapter and Python imports:

```bash
bluetoothctl show
bluetoothctl devices
python -c "import bluetooth; print('bluetooth module OK')"
python -c "import bleak; print('bleak OK')"
```

If `pybluez` fails to build on your platform, use `pybluez2` as a drop-in
provider for the same `bluetooth` module:

```bash
python -m pip install pybluez2 "bleak>=0.21.0"
```

Some raw Bluetooth operations may require elevated privileges or local adapter
configuration. Record the adapter model, BlueZ version, target firmware, and
target state when collecting reproducibility data.

## Windows

The Classic Bluetooth Python scripts are not natively supported on Windows
because they depend on Linux/BlueZ behavior exposed through the `bluetooth`
module. For Windows hosts, use WSL2 with a dedicated USB Bluetooth dongle passed
through to the Linux environment.

Microsoft documents the USB pass-through flow here:
[Connect USB devices under WSL](https://learn.microsoft.com/windows/wsl/connect-usb).

After the dongle is attached inside WSL2, follow the Linux quick start above and
confirm the adapter appears with `bluetoothctl show`.

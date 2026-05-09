# ESP32 Firmware Installation Guide

This guide is for building, flashing, and testing the ESP32 AVRCP console. The
same workflow applies on Windows and Linux: install Visual Studio Code, install
the Espressif ESP-IDF extension, use the extension wizard to install ESP-IDF,
open this repository, build the firmware, flash the ESP32, and test the serial
console.

If you do not have an ESP32 and want to run the Python proof-of-concept scripts
from a PC or laptop with a USB Bluetooth adapter, use
[PC/Laptop Bluetooth Adapter Setup](pc-laptop-bluetooth-setup.md) instead.

## Hardware Requirements

- ESP32-WROOM-32D development board, or another original ESP32 board.
- USB data cable.
- Soundcore R50i NC earbuds, or an owned Jieli-based Bluetooth audio device in
  scope for testing.

## Windows and Linux Setup

1. Download and install [Visual Studio Code](https://code.visualstudio.com/).

2. Install the ESP-IDF extension.
   Open VS Code, go to Extensions, search for `ESP-IDF`, and install the
   official Espressif IDF extension.

3. Start the ESP-IDF setup wizard.
   Press `F1`, search for `ESP-IDF: Configure ESP-IDF Extension`, and launch the
   wizard.

4. Install ESP-IDF through the wizard.
   Use the express setup unless you need a custom toolchain location. ESP-IDF
   v6.0 or newer is expected; local development notes currently target v6.1.

5. Download this repository.
   Clone it with Git or download the ZIP from GitHub:

   ```bash
   git clone https://github.com/SS-Sauron/Bluetooth-Jieli-Research.git
   ```

   ZIP download:
   <https://github.com/SS-Sauron/Bluetooth-Jieli-Research/archive/refs/heads/main.zip>

6. Open the firmware project in VS Code.
   Open this folder:

   ```text
   Bluetooth-Jieli-Research/firmware/esp32_avrcp_console
   ```

7. Select the ESP32 target.
   Press `F1`, run `ESP-IDF: Set Espressif Device Target`, and select `esp32`.

8. Build the firmware.
   Press `F1` and run `ESP-IDF: Build your Project`, or use the ESP-IDF status
   bar build button.

9. Flash the ESP32.
   Connect the board, select the serial port when prompted, then run
   `ESP-IDF: Flash your Project`.

10. Monitor and test the console.
    Run `ESP-IDF: Monitor your Device`. The interactive console should appear.

    ```text
    +-----------------------------+
    | Bluetooth Jieli Research    |
    | Main Menu                   |
    +-----------------------------+
    | > AVRCP                     |
    |   JL-SPP                    |
    |   Status                    |
    |   Reboot                    |
    +-----------------------------+
    W/S: move  A: back  D/Enter: select  Q/R: reboot
    ```

### Command-line alternative (no VS Code)

If you prefer not to use VS Code after ESP-IDF is installed, build and flash the
firmware from an ESP-IDF-enabled shell.

On Linux or macOS, source the ESP-IDF environment first:

```bash
source ~/esp-idf/export.sh
cd Bluetooth-Jieli-Research/firmware/esp32_avrcp_console
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

On Windows, open the `ESP-IDF CMD` shortcut created by the ESP-IDF installer,
then run:

```cmd
cd C:\Bluetooth-Jieli-Research\firmware\esp32_avrcp_console
idf.py set-target esp32
idf.py build
idf.py -p COM6 flash monitor
```

Replace `/dev/ttyUSB0` or `COM6` with the serial port for your ESP32 board.

## Android USB-OTG Serial Console

Android can control an already-flashed ESP32 over USB-OTG. Build and flash the
firmware from Windows or Linux first, then use Android only as the serial
console.

1. Enable a stable USB mode.
   In Android developer options, set the default USB configuration to USB
   tethering if your device supports it. This can help keep the OTG port active
   instead of switching to charge-only or file-transfer mode.

2. Install a serial bridge.
   `TCPUART transparent Bridge` and `Serial USB Terminal` are both common
   options from the Play Store.

3. Configure the bridge.
   Use baud rate `115200`. For TCP bridge mode, configure the phone as a server
   on port `8080`; direct serial mode is also fine if the app supports it.

4. Connect from Termux when using TCP bridge mode.

   ```bash
   pkg install socat screen
   socat pty,link=$HOME/esp32,raw,echo=0 tcp:127.0.0.1:8080 &
   screen $HOME/esp32 115200
   ```

The ESP32 interactive console should appear. The `W`, `S`, `A`, and `D` keys
work immediately.

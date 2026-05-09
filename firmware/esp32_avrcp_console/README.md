# ESP32 AVRCP Interactive Console

ESP‑IDF project implementing a modular, interactive console for injecting
AVRCP Volume Up/Down commands into Jieli‑based Bluetooth earbuds (target:
Soundcore R50i NC). The firmware now uses a Bruce-like WASD menu over serial
instead of typed shell commands.

## Build & Flash

Use the repository [ESP32 firmware installation guide](../../docs/installation.md)
for the VS Code and ESP-IDF extension workflow. The guide covers installing
ESP-IDF, opening this firmware project, building, flashing, and monitoring the
console.

## Menu Controls

The serial interface is menu-driven. Use the keyboard to move through the main
menu and select AVRCP, JL-SPP, Status, or Reboot actions.

| Key | Action |
| :--- | :--- |
| `W` | Move up |
| `S` | Move down |
| `A` | Go back |
| `D` or `Enter` | Select the highlighted item |
| `1`-`9` | Pick a numbered menu item directly |
| `Q` or `R` | Reboot the ESP32 |

## Hardware Requirements
- ESP32‑WROOM‑32D development board (or any original ESP32)
- USB data cable
- Soundcore R50i NC earbuds (or any Jieli‑based Bluetooth audio device)

## Platform Setup

See the repository [ESP32 firmware installation guide](../../docs/installation.md)
for Windows, Linux, and Android USB-OTG console notes. The Python PC/laptop
Bluetooth-adapter path is documented separately in
[PC/Laptop Bluetooth Adapter Setup](../../docs/pc-laptop-bluetooth-setup.md).

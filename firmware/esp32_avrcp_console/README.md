```markdown
# ESP32 AVRCP Interactive Console

ESP‑IDF project implementing a modular, interactive console for injecting
AVRCP Volume Up/Down commands into Jieli‑based Bluetooth earbuds (target:
Soundcore R50i NC).  Supports graceful connection management (`connect`,
`disconnect`), an abortable command queue, and a three‑command mental model:
`exit` (Ctrl+C), `reboot` (Ctrl+\), `disconnect` (hang‑up).

## Build & Flash

```bash
cd firmware/esp32_avrcp_console
idf.py set-target esp32
idf.py menuconfig   # enable Classic BT + BT L2CAP if needed
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Commands

| Command | Description |
|---------|-------------|
| `connect [MAC]` | Establish L2CAP/ACL link (default target used if no MAC) |
| `up [N]`        | Volume Up, N presses (default 15, 0 = do nothing) |
| `down [N]`      | Volume Down, N presses (default 15, 0 = do nothing) |
| `exit`          | Abort current batch (like Ctrl+C) |
| `disconnect`    | Gracefully tear down ACL link |
| `reboot`        | Force disconnect and restart ESP32 (like Ctrl+\) |
| `help`          | Show this list |

## Three‑Command Mental Model

* `exit` – interrupts the current operation, but keeps the connection alive (Ctrl+C)
* `reboot` – forcefully terminates the firmware and restarts the chip (Ctrl+\)
* `disconnect` – cleanly closes the Bluetooth connection without rebooting (hang‑up)
```

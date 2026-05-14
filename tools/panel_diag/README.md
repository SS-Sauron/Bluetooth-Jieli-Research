# Panel Diagnostic Tool

Zero‑dependency ESP‑IDF firmware that reads display and touch controller
registers, identifies known boards, and provides actionable troubleshooting.

## Usage

### 1. Edit the pin definitions for your board

Open `main/main.c` and update the `#define` lines at the top of the file
to match your board's wiring.  The default values are placeholders for the
Elecrow CrowPanel 2.8".  Common changes:

- `LCD_SCLK_GPIO`, `LCD_MOSI_GPIO`, `LCD_DC_GPIO` — SPI display pins
- `LCD_BL_GPIO` — backlight control (set to -1 if your board has none)
- `I2C_SDA_GPIO`, `I2C_SCL_GPIO` — touch controller I2C bus
- `LCD_H_RES`, `LCD_V_RES` — display resolution in pixels

If your board already appears in the known‑panel database (see below),
copy its pin assignments from `main.c`.

### 2. Find your serial port

**Linux:** `ls /dev/ttyUSB*` or `ls /dev/ttyACM*` — the ESP32‑S3 built‑in
USB‑JTAG port usually appears as `/dev/ttyACM0`.

**macOS:** `ls /dev/cu.*` — look for `/dev/cu.usbmodem*` or `/dev/cu.SLAB_USBtoUART*`.

**Windows:** Open Device Manager → Ports (COM & LPT).  Look for
"USB Serial Device" or "USB JTAG/serial debug unit".

### 3. Build and flash

```bash
cd tools/panel_diag
idf.py set-target esp32s3    # or esp32, esp32s2, esp32c3, etc.
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # replace with your port
```

### 4. Read the output

The tool prints a complete hardware report to the serial console at
115200 baud.  Each section includes raw register values AND plain‑English
interpretation.  If something fails, the output tells you exactly what
to check next.

## What it diagnoses

- Display controller type (ST7789, ILI9341, GC9A01)
- MADCTL register (mirror, scan direction, RGB/BGR order)
- I2C touch controller detection (FT6336U, GT911)
- Touch register verification (firmware ID, vendor ID, touch status)
- Known‑panel database (Elecrow, LilyGO, M5Stack, ESP‑BOX‑3, Waveshare)
- System info (chip revision, PSRAM, flash, free heap)
- Strapping pin check (GPIO 0)

## Adding your board

Open `main/main.c` and add an entry to the known‑panel database section.
Submit a PR so others can benefit.

## Need deeper help?

Paste your diagnostic output into the official Espressif AI chat:
  https://chat.espressif.com/

Or open an issue at your board vendor's repository with the diagnostic
output attached.

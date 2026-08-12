# LMYC Boat Terminal — Firmware

This directory contains the PlatformIO project for the **interactive cabin terminal** (Waveshare ESP32-S3-Touch-LCD-7).

It provides a clean, well-commented foundation with:
- Official ESP32_Display_Panel driver (LCD, GT911 touch, CH422G IO expander, backlight)
- LVGL v8 with a simple nautical-themed start screen
- Touch coordinate logging to Serial
- Correct PSRAM, flash, and USB CDC settings for this exact board

See the project docs for context:
- `../docs/03-system-architecture.md` — hybrid phone + terminal design
- `../docs/04-hardware.md` — hardware spec and safety notes
- `../docs/14-prototype-hardware.md` — prototype bring-up plan

**This is deliberately minimal.** NMEA, BLE pairing, full UI, logging, and E-Ink support will be added incrementally after this foundation is verified on hardware.

## Quick Start (macOS)

### Prerequisites
1. Install [PlatformIO Core](https://platformio.org/install/cli) (`brew install platformio` or via VS Code extension).
2. Connect the board via USB-C (it should appear as a serial device).

### Build & Upload
```bash
cd firmware

# Build only
pio run

# Build + upload (hold the BOOT button on the board during upload if it doesn't enter bootloader automatically)
pio run -t upload

# Monitor serial output (touch coordinates will appear here)
pio device monitor
```

### Common Commands
- `pio run -t clean` — clean build artifacts
- `pio run -t upload -t monitor` — upload then immediately open monitor
- `pio pkg update` — update libraries (LVGL, ESP32_Display_Panel, etc.)

### First-Time Tips
- The first upload may require holding the **BOOT** button while plugging in or pressing **RST** after upload.
- Serial output is at **115200 baud**.
- The screen should immediately show “LMYC Boat Terminal” with a teal box after boot.
- Touching the display will print `(x, y)` coordinates to the serial monitor.

### Project Structure
```
firmware/
├── platformio.ini          # Board config, libraries (LVGL v8 + official panel driver)
├── include/
│   └── lv_conf.h           # LVGL v8 configuration (800x480, widgets, fonts)
├── src/
│   └── main.cpp            # LVGL init, UI creation, touch handling
└── README.md
```

### Next Development Steps (after hardware validation)
1. Add LVGL styles/themes matching LMYC branding.
2. Build logbook entry form and live instrument gauges.
3. Implement QR code display for phone pairing.
4. Add LittleFS + TF card support for persistent logs.
5. Integrate isolated NMEA parsing (CAN/RS485) — **only after galvanic isolation hardware is in place**.

Contributions, safety feedback, and improvements are welcome. Keep the bus-isolation and single-volunteer-maintenance risks in mind (see `../docs/11-open-questions.md` and `../docs/13-risks-and-mitigations.md`).

Happy sailing!
```

**Hardware bring-up complete (display + touch foundation).** The firmware/ directory is ready. Connect the board, run `cd firmware && pio run -t upload -t monitor`, and verify the title screen + touch coordinates.

Let me know when the hardware arrives so we can iterate on the UI, add NMEA, or address any build/runtime issues. I can also expand this into a full structured LVGL UI with screens for logbook, instruments, and pairing.
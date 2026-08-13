# LMYC Boat Terminal — Firmware

PlatformIO project for the **interactive cabin terminal**:
[Waveshare ESP32-S3-Touch-LCD-7](https://www.waveshare.com/esp32-s3-touch-lcd-7.htm)
(7″ 800×480 RGB, GT911 capacitive touch, ESP32-S3 + OPI PSRAM).

## What this build does

- Brings up the RGB panel, backlight (CH422G), and GT911 touch via **ESP32_Display_Panel**
- Runs **LVGL v8** with the official port (`lvgl_v8_port`)
- Shows the **LMYC club logo** plus a **ready / QR pairing** screen
- **Wi-Fi as a client (STA)** — joins a configured LAN; it is **not** an access point
- **BLE GATT peripheral** + on-screen QR (`lmyc://pair?...`) for the pairing bootstrap
- Advertises as `LMYC-xxxx` (named, connectable, bondable). Android Settings can list it; iPhone Settings will not.
- **MicroSD (TF slot)** — mount FAT32 card (exFAT will not work)
- Credentials live in `src/secrets.h` (gitignored). Copy `src/secrets.h.example`.

NMEA, LE Secure Connections handshake, and booking-system talk are intentionally **not** included yet. The QR is scannable; the phone app that consumes it is next.

## Prerequisites (macOS)

```bash
brew install platformio
```

Connect the board with a **data-capable** USB-C cable (charge-only cables will not enumerate a serial port).

When plugged in you should see something like:

```text
/dev/cu.usbmodemXXXX
# or
/dev/cu.wchusbserialXXXX
```

Check with:

```bash
ls /dev/cu.*
pio device list
```

## Build & upload

```bash
cd firmware

# Build only
pio run

# Build + flash (hold BOOT if the board does not enter download mode)
pio run -t upload

# Serial log (115200 baud) — touch coordinates appear here
pio device monitor
```

One-shot:

```bash
pio run -t upload -t monitor
```

### First flash tips

1. Use the USB-C port on the board labelled for programming (see Waveshare wiki).
2. If upload fails with “Failed to connect”:
   - Hold **BOOT**
   - Press and release **RESET**
   - Release **BOOT**
   - Run `pio run -t upload` again
3. If no `/dev/cu.usb*` appears at all, try another cable/port. The Mac will not show the device for power-only cables.

## Project layout

```text
firmware/
├── platformio.ini          # Board + Arduino 3.x + libs
├── boards/                 # Custom board JSON (8 MB flash, OPI PSRAM)
├── assets/
│   └── lmyc-logo.png       # Source logo
├── scripts/                # (optional) image conversion helpers
├── src/
│   ├── main.cpp            # Splash UI
│   ├── lmyc_logo.c / .h    # Logo as LVGL RGB565+A image
│   ├── lvgl_v8_port.*      # Official LVGL port
│   ├── lv_conf.h
│   ├── esp_panel_*_conf.h  # Board = Waveshare ESP32-S3-Touch-LCD-7
│   └── esp_utils_conf.h
└── README.md
```

## Wi-Fi

This firmware is a **station / client**. It joins an existing network (marina / bench / home). It does not create its own hotspot.

```bash
cp src/secrets.h.example src/secrets.h
# edit WIFI_SSID and WIFI_PASSWORD
```

On the screen you should see SSID, connect status, IP, and RSSI. That is enough to prove the radio before any club-backend work.

## MicroSD

The Waveshare TF slot is **SPI** (GPIO 11/12/13) with chip-select on the **CH422G expander EXIO4**. Format the card **FAT32**. The status card lists the first few files at `/`.

## Regenerating the logo C array

If you replace `assets/lmyc-logo.png`:

```bash
python3 -m venv /tmp/lmyc-img && /tmp/lmyc-img/bin/pip install -q pillow
/tmp/lmyc-img/bin/python scripts/png_to_lvgl.py assets/lmyc-logo.png src/lmyc_logo
```

(Or use the same approach as the checked-in generator — RGB565 little-endian + alpha byte, `LV_IMG_CF_TRUE_COLOR_ALPHA`.)

## Docs

- `../docs/03-system-architecture.md`
- `../docs/04-hardware.md`
- `../docs/14-prototype-hardware.md`

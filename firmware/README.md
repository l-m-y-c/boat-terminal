# LMYC Boat Terminal — Firmware

PlatformIO project for the **interactive cabin terminal**:
[Waveshare ESP32-S3-Touch-LCD-7](https://www.waveshare.com/esp32-s3-touch-lcd-7.htm)
(7″ 800×480 RGB, GT911 capacitive touch, ESP32-S3 + OPI PSRAM).

## What this build does

- Brings up the RGB panel, backlight (CH422G), and GT911 touch via **ESP32_Display_Panel**
- Runs **LVGL v8** with the official port (`lvgl_v8_port`)
- Shows the **LMYC club logo** plus a **QR pairing** screen; after BLE pair it switches to a **helm** (depth / SOG / COG / wind, talkers, FIX chip)
- **Wi-Fi APSTA** — SoftAP `LMYC-<boat>` for the phone and Navionics, and STA to a configured LAN
- **BLE GATT peripheral** + on-screen QR (`lmyc://pair?...`) for the pairing bootstrap
- Advertises as `LMYC-xxxx`. The **complete local name is in the primary advertising packet** (the 128-bit LMYC UUID is only in the scan response). Android Settings often still hide LE-only devices — use the LMYC app **Find terminals**.
- On-screen **DIAGNOSTICS** card: BLE status, MAC, connect count, last event, ADV layout, Wi-Fi / IP / RSSI, heap, SD, plus a **Reset BLE** button
- **MicroSD (TF slot)** — mount FAT32 card (exFAT will not work)
- Credentials live in `src/secrets.h` (gitignored). Copy `src/secrets.h.example`.

Bench pairing is QR + BLE OOB via the Flutter app in `../mobile`. **Slice A** adds an NMEA 0183 simulator (FIX: SIM), TCP gateway on port **10110**, and BLE notify `…000030` for a phone sentence console. Real RS485 listen is TODO(slice-d).

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
│   ├── main.cpp            # UI shell + timers (keep thin)
│   ├── pairing.*           # BLE identity, OOB, GATT
│   ├── nmea.*              # Line assembler + checksum
│   ├── nmea_sim.*          # 1 Hz canned talker (FIX: SIM)
│   ├── nmea_state.*        # Latest fix / depth / wind for the helm
│   ├── nmea_io.*           # UART/RS485 (sim-only until slice-d)
│   ├── gateway.*           # TCP/UDP 10110
│   ├── hub_http.*          # GET /  POST /snapshot  POST /inject
│   ├── snapshot.*          # 800×480 JPEG for the Chart page
│   ├── inject.*            # Phone GNSS, armed only from the 7″
│   ├── lmyc_logo.c / .h    # Logo as LVGL RGB565+A image
│   ├── lvgl_v8_port.*      # Official LVGL port
│   ├── lv_conf.h
│   ├── esp_panel_*_conf.h  # Board = Waveshare ESP32-S3-Touch-LCD-7
│   └── esp_utils_conf.h
└── README.md
```

### BoatLink (Navionics + snapshots)

The terminal runs **APSTA**: SoftAP `LMYC-<boat>` (password `AP_PASSWORD` in `secrets.h`, default `lmycboat`) **and** STA to your bench LAN.

| Path | Use |
|------|-----|
| TCP **192.168.4.1:10110** | Navionics / OpenCPN / `nc` |
| UDP 10110 and 2000 | Broadcast of the same sentences |
| `http://192.168.4.1/` | How-to page |
| `POST /snapshot` | 800×480 JPEG, ≤220 KB, 1 fps. Show with **Chart** on the helm |
| `POST /inject` | Phone GNSS RMC/GGA — only if the 7″ **GNSS LOCAL** button is armed |

No RS485 TX. Sim still provides depth/wind when GNSS is LOCAL.

### NMEA (Slice A)

With no RS485 talker, firmware emits `$GPRMC` / `$GPGGA` / `$GPDPT` / `$WIMWV` at 1 Hz and labels the UI **FIX: SIM**. Sentences go to:

- TCP **10110** (CRLF) for `nc <ip> 10110` / Navionics later
- BLE notify `6c6d7963-0001-4000-8000-000000000030` (dropped if longer than ATT payload)

Pairing GATT (`…000020` / `…000021`) is unchanged.


## Wi-Fi

The firmware runs **APSTA**.

- **SoftAP** `LMYC-<boat id>` lets a phone or chart app join when there is no marina network. The password is `AP_PASSWORD` in `secrets.h`.
- **STA** joins the bench or marina LAN from `WIFI_SSID` and `WIFI_PASSWORD`.

```bash
cp src/secrets.h.example src/secrets.h
```

The screen shows the STA SSID, connect status, IP, and RSSI. The SoftAP address is `192.168.4.1`.

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

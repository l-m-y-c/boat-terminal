# LMYC Boat Terminal — Firmware

PlatformIO project for the **interactive cabin terminal**:
[Waveshare ESP32-S3-Touch-LCD-7](https://www.waveshare.com/esp32-s3-touch-lcd-7.htm)
(7″ 800×480 RGB, GT911 capacitive touch, ESP32-S3 + OPI PSRAM).

## What this build does

- Brings up the RGB panel, backlight (CH422G), and GT911 touch via **ESP32_Display_Panel**
- Runs **LVGL v8** with the official port (`lvgl_v8_port`)
- The top bar stays on every screen, including a maintenance photo. Left side: a position icon, a link icon, and a **QR** button, each 40 px. Tap one and the full line opens under the bar. Tap it again, tap the page, or change screen, and the line closes. The landing screen fills the page: the club logo on the left half, the pairing QR on the right. After the phone pairs, the QR parks and the QR button brings that same code back. Right side: **Home**, always in the same place, then the breadcrumb. **Home** always returns to the landing screen. In the book the path is **Reports**, then **Details**, then **Photo**. The step you are on is highlighted. **Reports** returns to the list. **Details** returns to that report. Screen buttons live on a rail at the right edge. Closed, a 44 px tab stays on the glass, marked `<`. Tap that tab, or swipe left on the page, and the rail slides out over the page. Swipe right closes it. With the rail shut, swipe right on a report moves to the next open report and wraps back to the first, and swipe right on a maintenance photo flips between the picture and its note. A vertical drag still scrolls the report. The full-text reader stays on that report. On the landing screen the rail is **Charts**, **Reports**, **Instruments**, and **Diagnostics**. Pairing stays on the landing screen. Instruments open from that rail, with or without a phone.
- **Wi-Fi APSTA** — SoftAP `LMYC-<boat>` for the phone and Navionics, and STA to a configured LAN
- **BLE GATT peripheral** + on-screen QR (`lmyc://pair?...`) for the pairing bootstrap
- Advertises as `LMYC-xxxx`. The **complete local name is in the primary advertising packet** (the 128-bit LMYC UUID is only in the scan response). Android Settings often still hide LE-only devices — use the LMYC app **Find terminals**.
- **Diagnostics** is its own page (radio, card, Wi-Fi, heap, NMEA). **Reset BLE** is the top button on that screen's rail
- **MicroSD (TF slot)** — mount FAT32 card (exFAT will not work)
- Credentials live in `src/secrets.h` (gitignored). Copy `src/secrets.h.example`.

Bench pairing is QR + BLE OOB via the Flutter app in `../mobile`. **Slice A** adds an NMEA 0183 simulator (FIX: SIM), TCP gateway on port **10110**, and BLE notify `…000030` for a phone sentence console. Real RS485 listen is TODO(slice-d).

## Prerequisites (macOS)

```bash
brew install platformio
```

Connect the board with a **data-capable** USB-C cable (charge-only cables will not enumerate a serial port). List ports with `ls /dev/cu.*`.

The board has two Type-C sockets.

- **USB** is the ESP32's own USB, and it is the socket that accepts a flash on a Mac. It is shared with the CAN connector. Plugging it in normally does not create a serial port.
- **UART** is a CH343 USB-serial chip (`1A86:55D3`). On this Mac it appears as `/dev/cu.usbmodem5C372493411`, product name "USB Single Serial". `make flash` auto-detects that port. esptool then reports `No serial data received`. Holding BOOT and tapping RESET on that socket produced no bytes either. macOS attaches its built-in USB serial driver to the CH343.

To flash, use the socket marked **USB**:

1. Unplug the cable from that socket.
2. Hold **BOOT**.
3. Plug the cable into the socket marked **USB**.
4. Release **BOOT**.
5. The Mac shows a new port, product "USB JTAG/serial debug unit" (`303A:1001`). On 2026-10-01 that was `/dev/cu.usbmodem101`. The number changes.

```bash
ls /dev/cu.*
cd firmware
pio run -t upload --upload-port /dev/cu.usbmodem101
```

`make flash` runs `pio run -t upload` and does not name a port. When the CH343 port is the one that enumerated, that command selects it and the upload fails. Pass `--upload-port` for the JTAG/serial device.

After a successful upload the tool resets the chip. Press **RESET** if the screen stays blank. The pairing QR changes every boot, so scan it again.

Leave the cable in the UART socket once the program is running. Opening the USB-JTAG port from the Mac resets the chip into download mode.

## Display

The panel stays up when LVGL is fed the way the first firmware did (`964d690`), confirmed on the bench on 2026-10-01:

- Two draw buffers in PSRAM, 40 rows tall (`LVGL_PORT_BUFFER_MALLOC_CAPS` / `LVGL_PORT_BUFFER_SIZE_HEIGHT` in `src/lvgl_v8_port.h`).
- `lvgl_port_handle()` from `loop()` on the same core that called `Board::begin()`. No LVGL task.
- `create_ui()` runs before the TF card, BLE, and Wi-Fi.

The Waveshare board file supplies the panel timing (800×480, 16 MHz, 10-line bounce). Do not retune the pixel clock, DMA burst, or bounce buffer. Moving those buffers into internal RAM and running LVGL on its own task made the picture walk off the glass and the chip reboot.

That procedure wrote the maintenance-book firmware on 2026-10-01. esptool identified an ESP32-S3 (QFN56) revision v0.2 with 8 MB embedded PSRAM and a 16 MB flash, MAC `58:e6:c5:72:d6:48`. The image hash verified.

## Build & upload

```bash
cd firmware

# Build only
pio run

# Flash through the USB socket after the BOOT-plug steps above
pio run -t upload --upload-port /dev/cu.usbmodem101

# Serial log (115200 baud) — touch coordinates appear here
pio device monitor
```

## Project layout

```text
firmware/
├── platformio.ini          # Board + Arduino 3.x + libs
├── boards/                 # Custom board JSON (8 MB flash, OPI PSRAM)
├── assets/
│   └── lmyc-logo.png       # Source logo
├── scripts/                # (optional) image conversion helpers
├── test_host/              # Host tests, no board: `make firmware-test`
├── src/
│   ├── main.cpp            # UI shell + timers (keep thin)
│   ├── boat_config.h       # Per-boat settings. The only file that includes secrets.h
│   ├── protocol.h          # UUIDs, ports, book limits shared with the app (checked in CI)
│   ├── net_wifi.*          # SoftAP + STA, reconnect backoff, starts gateway + HTTP
│   ├── sd_card.*           # TF card mount (CS on the IO expander)
│   ├── pairing.*           # BLE identity, OOB, GATT
│   ├── nmea.*              # Line assembler + checksum
│   ├── nmea_sim.*          # 1 Hz canned talker (FIX: SIM)
│   ├── nmea_state.*        # Latest fix / depth / wind for the helm
│   ├── nmea_io.*           # Console bytes, NMEA SIM|N2K|AUTO, later RS485
│   ├── n2k.*               # NMEA 2000 listen-only (no transmit)
│   ├── n2k_to_0183.*       # PGN → 0183 into the existing sink
│   ├── ais0183.*           # !AIVDM / !AIVDO packer
│   ├── gateway.*           # TCP/UDP 10110
│   ├── hub_http.*          # GET /  POST /snapshot  POST /inject  /maintenance
│   ├── snapshot.*          # 800×480 JPEG for the Chart page and book photos
│   ├── inject.*            # Phone GNSS, armed only from the 7″
│   ├── maint_store.*       # This boat's maintenance book on the TF card
│   ├── json_lite.*         # Flat JSON reader/writer for item.json (host-tested)
│   ├── maint_ui.*          # Open-item picker, item detail, photo thumbnails
│   ├── lmyc_logo.c / .h    # Logo as LVGL RGB565+A image
│   ├── lvgl_v8_port.*      # Official LVGL port
│   ├── lv_conf.h
│   ├── esp_panel_*_conf.h  # Board = Waveshare ESP32-S3-Touch-LCD-7
│   └── esp_utils_conf.h
└── README.md
```

### Which task runs what

Five FreeRTOS tasks touch this code. Shared state needs an owner or a lock.

| Task | Runs | Owns |
|------|------|------|
| Arduino `loop()` | `nmea_io`, `n2k`, `nmea_sim`, `inject_poll`, `gateway`, `net_wifi_poll`, book photo decodes | The NMEA parser and its sink (`nmea_state`, gateway sockets, BLE tap) |
| LVGL (`lvgl_v8_port`) | Every `lv_*` call, the 1 s status timer, `maint_ui` | All LVGL objects. Other tasks take `lvgl_port_lock()` first |
| HTTP (`esp_http_server`) | `hub_http` handlers | Nothing. It queues GNSS for `loop()` (`inject_submit`) |
| NimBLE host | `pairing` callbacks | The pairing status strings |
| Wi-Fi events | `net_wifi` event handler | Flags only. Services start from `net_wifi_poll()` |

`snapshot` decodes and `maint_store` card access are locked internally, because both the HTTP task and `loop()`/LVGL use them. A book listing uses a `MaintListCursor` owned by the caller.

### BoatLink (Navionics + snapshots)

The terminal runs **APSTA**: SoftAP `LMYC-<boat>` (password `AP_PASSWORD` in `secrets.h`, default `lmycboat`) **and** STA to your bench LAN.

| Path | Use |
|------|-----|
| TCP **192.168.4.1:10110** | Navionics / OpenCPN / `nc` |
| UDP 10110 and 2000 | Broadcast of the same sentences |
| `http://192.168.4.1/` | How-to page |
| `POST /snapshot` | 800×480 JPEG, ≤220 KB, 1 fps. Show with **Charts** on the right-hand rail |
| `POST /inject` | Phone GNSS RMC/GGA — only if the 7″ **GNSS LOCAL** button is armed |
| `GET /maintenance` | This boat's maintenance book on the TF card (no cell required) |
| `POST /maintenance` | File a pending item (`title`, `category`, `priority`, `description`) |
| `GET /maintenance/item?id=` | One `item.json` |
| `GET` or `POST /maintenance/photo?id=&n=` | JPEG `n` (1–5), up to 500 KB. Optional `note` (120 characters) is stored as `noteN` on the item. The phone keeps the long edge near 2000 px |
| `POST /maintenance/ack?id=` | JSON `{"wp_id":N}` after the club accepts the item. Sets `sync` to `synced` |
| `POST /maintenance/reconcile` | Apply one club edit. The website `modified` time wins. A pending boat note is left unchanged |

**Reports** on the rail lists open items (completed and future-wishlist are skipped). The list is one screen: a header names Title, Category, and Priority, and each row is one line in those columns. A camera mark in its own column means that report has photos. Opening Reports reads the open items from the card once and keeps that copy in RAM until the phone writes the book. The photos themselves are decoded from the card when a report is opened. A long title or category ends in an ellipsis. Priority is a chip with the word on it: Critical and High are amber, Medium is teal, Low is gray. A mounted card with nothing open says **No open reports**. An empty slot says **No card in the slot**. Tap a row for that item. The item is wells: the field name is small and gray, the value is white. Category, priority, status, and reporter sit in a two-by-two band, and priority uses the same chip. The description well shows about four lines. The page scrolls, and that is the only scrollbar. When the title or the description is longer than its well, tap it. A reader covers the page with the full text and its own scroll. **Close** returns to the same report. Swipe right on that page for the next open report, wrapping after the last. One report stays put, and the reader stays on its report. Photos stack under that. Each picture takes half the width and keeps its shape. The note fills a well of the same height beside it, clipped when it is long. Tap the picture to open that JPEG on the whole glass, on the picture. Tap the note to open it already on the note. Swipe right to move between the photo and the note; the two keep rotating. Swipe left opens the rail. The note screen says **No note for this photo** when none was stored. **Back** on the chart rail returns to the item and does not reload the list. **Next photo** steps through that item and lands back on the picture. **Items** on the reports rail returns to the list. The breadcrumb reads **Reports** on the list, **Reports > Details** on a report, and **Reports > Details > Photo** on a picture. **Reports** reloads the list. **Details** returns to the open report. **Home** in the top bar always returns to the landing screen. **Debug** on the reports rail lists the last HTTP requests. The chart snapshot and the book share one canvas: opening a maintenance photo replaces the chart image until the next snapshot arrives. **Charts** from home shows that snapshot fitted to the page, as one picture, and **Back** returns home.

No RS485 TX. Sim still provides depth/wind when GNSS is LOCAL.

### NMEA (Slice A)

With no RS485 talker, firmware emits `$GPRMC` / `$GPGGA` / `$GPDPT` / `$WIMWV` at 1 Hz and labels the UI **FIX: SIM**. Sentences go to:

- TCP **10110** (CRLF) for `nc <ip> 10110` / Navionics later
- BLE notify `6c6d7963-0001-4000-8000-000000000030` (dropped if longer than ATT payload)

**Instruments** is Depth, Speed, Course, and Wind, four tiles that fill the page. A list icon and a Wi-Fi icon at the top right open NMEA talkers and the link summary on their own screens. **Close** returns to the instruments. The full radio and boat detail stays on Diagnostics.

### NMEA 2000 listen-only

`NMEA_SOURCE` in `secrets.h` (copied from `secrets.h.example`):

| Value | Behavior |
|-------|----------|
| `0` | Simulator. USB serial stays up. Default for the bench and for CI. |
| `1` | Listen on the CAN header. No transmit. |
| `2` | Simulator until a CAN frame arrives, then the bus. |

On the serial console: `NMEA SIM`, `NMEA N2K`, or `NMEA AUTO`.

The header is GPIO20 TX / GPIO19 RX through the onboard TJA1051. Those pins are also USB. CH422G EXIO5 high selects CAN, and the log moves to the UART Type-C socket (GPIO43/44). Remove the 120 Ω terminator jumper. The onboard transceiver is **not isolated**. On the boat, power the board from the NMEA 2000 drop (NET-S / NET-C through a fuse and a 12 V to 5 V DC/DC into the USB socket) and wire nothing else to it: no USB lead, RS485 or other 12 V while it's on the bus. Flash and read the log off the bus. If the board must keep its own power, it needs an isolated CAN interface instead. Wiring is in [doc 19](../docs/19-n2k-backbone-install.md#4-boat-terminal-connection). Details and the PGN → 0183 table are in [doc 05](../docs/05-nmea-connectivity.md).

The FIX chip reads `SIM`, `N2K` (selected, no frames yet), `BOAT` (frames flowing), or `PHONE` (gated GNSS). Depth, speed, course, and wind tiles update from the converted sentences. Phones keep using TCP 10110 and the BLE NMEA notify.

Pairing GATT is `…000020` / `…000021`. After OOB OK, `…000040` serves the SoftAP name, passphrase, and `192.168.4.1` so the phone can join. That read is `WAIT` until the pair succeeds.


## Wi-Fi

The firmware runs **APSTA**.

- **SoftAP** `LMYC-<boat id>` lets a phone or chart app join when there is no marina network. The password is `AP_PASSWORD` in `secrets.h`. The LMYC app receives it over BLE `…000040` after pairing and asks Android to connect. Navionics joins the same SSID from Wi-Fi settings.
- **STA** joins the bench or marina LAN from `WIFI_SSID` and `WIFI_PASSWORD`.

```bash
cp src/secrets.h.example src/secrets.h
```

The screen shows the STA SSID, connect status, IP, and RSSI. The SoftAP address is `192.168.4.1`.

## MicroSD

The Waveshare TF slot is **SPI** (GPIO 11/12/13) with chip-select on the **CH422G expander EXIO4**. Format the card **FAT32**. The status card lists the first few files at `/`. After a successful mount the firmware creates `/maint` and serves this boat's maintenance book on `GET /maintenance`. See [doc 18](../docs/18-maintenance-handoff-and-auth.md).

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

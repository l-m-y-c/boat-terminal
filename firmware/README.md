# LMYC Boat Terminal — Firmware

PlatformIO project for the **interactive cabin terminal**:
[Waveshare ESP32-S3-Touch-LCD-7](https://www.waveshare.com/esp32-s3-touch-lcd-7.htm)
(7″ 800×480 RGB, GT911 capacitive touch, ESP32-S3 + OPI PSRAM).

## What this build does

- Brings up the RGB panel, backlight (CH422G), and GT911 touch via **ESP32_Display_Panel**
- Runs **LVGL v8** with the official port (`lvgl_v8_port`)
- **One build switch.** `LMYC_WITH_REPORTS_CHARTS` in `src/boat_config.h` is `0` by default: instruments, the data source, and the NMEA repeater to chart apps. `1` adds the maintenance book (**Reports**), the chart picture (**Charts**), the TF card store, and their HTTP routes. Pass `-DLMYC_WITH_REPORTS_CHARTS=1` (for example `PLATFORMIO_BUILD_FLAGS="-DLMYC_WITH_REPORTS_CHARTS=1" pio run`) to build them.
- **Landing screen = the journey.** The same four stops as the phone app across the top: **Find**, **Connect**, **Wi-Fi**, **Chart data**. Done stops are ticked green, the current one is ringed, a problem is red. Under it, one title and one sentence on the left, and the pairing QR on the right while a phone has to scan it (the club logo otherwise). Arrival reads **On board <boat>** once a phone is on the hotspot and the LMYC app's relay is connected to TCP 10110; it does not need Bluetooth. The **QR** chip on the top bar brings the code back for another phone. See [doc 06](../docs/06-phone-app-and-pairing.md#the-journey).
- The top bar stays on every screen. Left: a position icon, a link icon, and the **QR** chip, each 40 px; tap one and the full line opens under the bar. Right: **Home**, then the page name. Screen buttons live on a rail at the right edge behind a 44 px `<` tab; tap it or swipe left. On the landing screen the rail is **Instruments** and **Diagnostics** (plus **Charts** and **Reports** in the full build).
- **Wi-Fi** — SoftAP `LMYC-<boat>` for phones and chart apps, and optionally STA to a configured LAN
- **BLE GATT peripheral** + on-screen QR (`lmyc://pair?...`) for the pairing bootstrap
- Advertises as `LMYC-xxxx`. The **complete local name is in the primary advertising packet** (the 128-bit LMYC UUID is only in the scan response). Android Settings often still hide LE-only devices — use the LMYC app **Find terminals**.
- **Diagnostics** is its own page (radio, card, Wi-Fi, heap, NMEA). **Reset BLE** is the top button on that screen's rail
- **MicroSD (TF slot)**, full build only — mount FAT32 card (exFAT will not work)
- Credentials live in `src/secrets.h` (gitignored). Copy `src/secrets.h.example`.

Bench pairing is Discover or QR + BLE OOB via the Flutter app in `../mobile`. Instrument data comes from one of two sources, chosen on the glass and saved: a **simulator** (a virtual NMEA 2000 bus) or the boat's **NMEA 2000** backbone, listen-only. The top bar says which on every screen. Data goes out on TCP **10110**, UDP 10110/2000, and BLE notify `…000030`. Real RS485 listen is TODO(slice-d).

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

- LVGL's own heap is PSRAM too (`src/lv_mem_psram.h`). Widgets are small allocations, which the IDF keeps in internal RAM, and every page is built at boot; in internal RAM they took about 96 KB that BLE and Wi-Fi need. With Bluetooth up, about 180 KB of internal RAM is left before Wi-Fi starts.
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
│   ├── nmea_state.*        # Latest fix / depth / wind for the helm
│   ├── nmea_io.*           # USB + UART0 console, NMEA SIM|N2K|STATS|DEMO|PGN
│   ├── n2k.*               # Data source, CAN receive task, frame queue, counters (no transmit)
│   ├── n2k_sim.*           # Simulator on a virtual bus, CAN framing (host-tested)
│   ├── terminal_stats.*    # Counters as JSON for BLE …000050 and /status.json
│   ├── n2k_to_0183.*       # PGN → 0183 into the existing sink
│   ├── ais0183.*           # !AIVDM / !AIVDO packer
│   ├── gateway.*           # TCP/UDP 10110, an output queue per chart app
│   ├── hub_http.*          # GET /  POST /snapshot  POST /inject  /maintenance
│   ├── snapshot.*          # 800×480 JPEG for the Chart page and book photos
│   ├── inject.*            # Phone GNSS, armed only from the 7″
│   ├── maint_store.*       # This boat's maintenance book on the TF card
│   ├── json_lite.*         # Flat JSON reader/writer for item.json (host-tested)
│   ├── maint_ui.*          # Open-item picker, item detail, photo thumbnails
│   ├── lmyc_logo.c / .h    # Logo as LVGL RGB565+A image
│   ├── lvgl_v8_port.*      # Official LVGL port
│   ├── lv_mem_psram.h      # LVGL heap in PSRAM, internal RAM only as a fallback
│   ├── lv_conf.h
│   ├── esp_panel_*_conf.h  # Board = Waveshare ESP32-S3-Touch-LCD-7
│   └── esp_utils_conf.h
└── README.md
```

### Which task runs what

Seven FreeRTOS tasks touch this code. Shared state needs an owner or a lock.

| Task | Runs | Owns |
|------|------|------|
| Arduino `loop()` | `nmea_io`, `n2k_poll` (drains the frame queue), `inject_poll`, `gateway`, `net_wifi_poll`, `terminal_stats_refresh`, book photo decodes | The NMEA 2000 stack, the NMEA parser and its sink (`nmea_state`, gateway sockets, BLE tap) |
| `n2k_rx` (NMEA 2000 only) | TWAI receive, driver counters, bus-off handling | The CAN controller. Frames go to `loop()` through the frame queue |
| `n2k_sim` (simulator only) | `n2k_sim_step` every 10 ms | The simulator's state. Frames go through the same queue |
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
| `GET /status.json` | Boat id, delivery counters, and the data-source statistics |
| `POST /snapshot` | Full build. 800×480 JPEG, ≤220 KB, 1 fps. Show with **Charts** on the right-hand rail |
| `POST /inject` | Phone GNSS RMC/GGA — only if the 7″ **GNSS LOCAL** button is armed |
| `GET /maintenance` | Full build. This boat's maintenance book on the TF card (no cell required) |
| `POST /maintenance` | File a pending item (`title`, `category`, `priority`, `description`) |
| `GET /maintenance/item?id=` | One `item.json` |
| `GET` or `POST /maintenance/photo?id=&n=` | JPEG `n` (1–5), up to 500 KB. Optional `note` (120 characters) is stored as `noteN` on the item. The phone keeps the long edge near 2000 px |
| `POST /maintenance/ack?id=` | JSON `{"wp_id":N}` after the club accepts the item. Sets `sync` to `synced` |
| `POST /maintenance/reconcile` | Apply one club edit. The website `modified` time wins. A pending boat note is left unchanged |

The `/maintenance` routes and **Reports** are in the full build only. **Reports** on the rail lists open items (completed and future-wishlist are skipped). The list is one screen: a header names Title, Category, and Priority, and each row is one line in those columns. A camera mark in its own column means that report has photos. Opening Reports reads the open items from the card once and keeps that copy in RAM until the phone writes the book. The photos themselves are decoded from the card when a report is opened. A long title or category ends in an ellipsis. Priority is a chip with the word on it: Critical and High are amber, Medium is teal, Low is gray. A mounted card with nothing open says **No open reports**. An empty slot says **No card in the slot**. Tap a row for that item. The item is wells: the field name is small and gray, the value is white. Category, priority, status, and reporter sit in a two-by-two band, and priority uses the same chip. The description well shows about four lines. The page scrolls, and that is the only scrollbar. When the title or the description is longer than its well, tap it. A reader covers the page with the full text and its own scroll. **Close** returns to the same report. Swipe right on that page for the next open report, wrapping after the last. One report stays put, and the reader stays on its report. Photos stack under that. Each picture takes half the width and keeps its shape. The note fills a well of the same height beside it, clipped when it is long. Tap the picture to open that JPEG on the whole glass, on the picture. Tap the note to open it already on the note. Swipe right to move between the photo and the note; the two keep rotating. Swipe left opens the rail. The note screen says **No note for this photo** when none was stored. **Back** on the chart rail returns to the item and does not reload the list. **Next photo** steps through that item and lands back on the picture. **Items** on the reports rail returns to the list. The breadcrumb reads **Reports** on the list, **Reports > Details** on a report, and **Reports > Details > Photo** on a picture. **Reports** reloads the list. **Details** returns to the open report. **Home** in the top bar always returns to the landing screen. **Debug** on the reports rail lists the last HTTP requests. The chart snapshot and the book share one canvas: opening a maintenance photo replaces the chart image until the next snapshot arrives. **Charts** from home shows that snapshot fitted to the page, as one picture, and **Back** returns home.

No RS485 TX. The simulator still provides depth and wind when GNSS is LOCAL.

### NMEA out

Both sources come out of the same converter as 0183 sentences, about once a second per instrument, and AIS as it arrives. Sentences go to:

- TCP **10110** (CRLF) for `nc <ip> 10110` / Navionics later
- UDP broadcast to `192.168.4.255` ports 10110 and 2000
- BLE notify `6c6d7963-0001-4000-8000-000000000030` (dropped if longer than ATT payload)

Each value on the Instruments tiles goes blank (`—`) when it is more than 5 s old.

**Instruments** is Depth, Speed, Course, and Wind, four tiles that fill the page. The readings are Montserrat 96. Captions and units stay at 16. The crumb on that page adds the fix tag (`SIM`, `PLAY`, `N2K`, `BOAT`, or `PHONE`). A list icon and a Wi-Fi icon at the top right open NMEA detail and the link summary on their own screens. The Wi-Fi icon is amber while no phone is associated to the hotspot and green when one is. The link screen reads Bluetooth, the hotspot name, phones on it, NMEA listeners on TCP 10110, and Uplink for the terminal's own upstream network. The list shows the NMEA 2000 types seen on the bus, then the 0183 talkers. **Close** returns to the instruments. The full radio and boat detail stays on Diagnostics.

### Data source

The badge on the top bar reads **SIMULATION** (amber), **LIVE NMEA 2000** (green), or **NMEA 2000 NO DATA** / **BUS FAULT** (red), and the line under the bar matches. Tap it for **Data source** (also on the Diagnostics rail): the two sources, the active one marked, and the bus counters: messages and frames a second, peak, lost frames, buffer peak against 2048, bus errors, PGN types, devices, chart apps, and sentences delivered. Choose the other source and confirm; the choice is saved and the terminal restarts into it.

`NMEA_SOURCE` in `secrets.h` is only the first-boot default (`0` simulator, `1` NMEA 2000; the old `2` AUTO now means `1`). A live bus never falls back to simulated data.

On the serial console: `NMEA SIM` or `NMEA N2K` (save and restart), `NMEA STATS`, `NMEA DEMO`, or `NMEA PGN <number> [src=N] <hex>`. `DEMO` and `PGN` run only on the simulator: they pause it and feed reassembled PGNs through the converter. The badge reads **SIMULATION PAUSED** and the fix tag `PLAY`. `make n2k-demo` (or `python3 tools/n2k_play.py`) writes `NMEA DEMO` to the UART Type-C socket, the CH343 on GPIO43/44. Leave the cable in that socket. Opening the USB socket from the Mac resets the chip into download. The demo is depth 42.0 m, course 90°, speed 7.5 kn, wind 18 kn at 40° apparent, heading 45° true, and PGN 130816 so an unknown type is listed. `NMEA SIM` resumes the simulator. `python3 tools/n2k_play.py PGN 130816 src=4 dead` sends one hex payload.

The receive path, and why it loses nothing, is in [doc 05](../docs/05-nmea-connectivity.md#receive-path-nothing-is-dropped-uncounted).

The header is GPIO20 TX / GPIO19 RX through the onboard TJA1051. Those pins are also USB. CH422G EXIO5 high selects CAN, and the log moves to the UART Type-C socket (GPIO43/44). Remove the 120 Ω terminator jumper. The onboard transceiver is **not isolated**. On the boat, power the board from the NMEA 2000 drop (NET-S / NET-C through a fuse and a 12 V to 5 V DC/DC into the USB socket) and wire nothing else to it: no USB lead, RS485 or other 12 V while it's on the bus. Flash and read the log off the bus. If the board must keep its own power, it needs an isolated CAN interface instead. Wiring is in [doc 19](../docs/19-n2k-backbone-install.md#4-boat-terminal-connection). Details and the PGN → 0183 table are in [doc 05](../docs/05-nmea-connectivity.md).

The FIX chip reads `SIM`, `PLAY` (simulator paused for canned PGNs), `N2K` (NMEA 2000 chosen, nothing arriving), `BOAT` (frames flowing), or `PHONE` (gated GNSS). Tap that icon and the line under the bar adds the source (`NMEA 2000 - Live - 312 frames/s - 0 lost`) and each PGN seen: number, name, source address, age, and how many. The same list is under the Instruments list icon. Phones keep using TCP 10110 and the BLE NMEA notify.

Pairing GATT is `…000020` / `…000021`. After OOB OK, `…000040` serves the SoftAP name, passphrase, and `192.168.4.1` so the phone can join. That read is `WAIT` until the pair succeeds.


## Wi-Fi

- **SoftAP** `LMYC-<boat id>` is the boat Wi-Fi. The password is `AP_PASSWORD` in `secrets.h`. The LMYC app receives it over BLE `…000040` after pairing and saves it with Android's prompt. Chart apps on a phone use UDP 10110 (address `127.0.0.1` where asked) through the app; see [doc 06](../docs/06-phone-app-and-pairing.md#why-chart-apps-use-the-phone-itself).
- **STA** (optional) joins a bench or marina LAN from `WIFI_SSID` and `WIFI_PASSWORD`. An empty `WIFI_SSID` runs the hotspot alone. Each connect attempt scans every channel and takes the radio off the hotspot channel, which stalls chart apps and BLE, so a network that is not there is tried three times and then left until the next restart (`Wi-Fi: uplink '<ssid>' not found, hotspot only until restart`).

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

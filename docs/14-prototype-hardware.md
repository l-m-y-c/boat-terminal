# 14 – Prototype Hardware (Ordered)

**Status:** Hardware ordered  
**Board:** Waveshare **ESP32-S3-Touch-LCD-7** (7" 800×480 capacitive touch)

This is the first physical prototype unit for the LMYC Boat Terminal project.

---

## Why this board was chosen

| Feature | Present on this board | Relevance |
|---------|-----------------------|-----------|
| 7" capacitive touch display | Yes (800×480) | Good size for cabin logbook + instruments |
| ESP32-S3 dual-core | Yes | Sufficient for LVGL UI + NMEA parsing |
| **CAN header** | Yes | NMEA 2000 prototyping |
| **RS485 header** | Yes | NMEA 0183 |
| TF card slot | Yes | Local log storage |
| I2C + sensor header | Yes | Expansion |
| Wi-Fi + BLE 5 | Yes | QR pairing + phone companion |
| Official enclosure | No | Community 3D-print: [wall-mount case on Printables](https://www.printables.com/model/1558735-waveshare-esp32-s3-touch-lcd-7-case). See [19](19-n2k-backbone-install.md#2-parts-list). |
| Wide 7–36 V input | No | Needs external DC-DC from 12 V. See [20](20-n2k-bus-power-supply.md). |
| Galvanic isolation on CAN/RS485 | No | Open compliance item: an isolated CAN transceiver is needed for a fully compliant NMEA 2000 node |

This board gives us a large usable screen and the native interfaces needed to start real NMEA work quickly.

---

## Official Documentation

**Primary sources (start here):**

- Product Wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7
- Documentation platform: https://docs.waveshare.com/ESP32-S3-Touch-LCD-7

These pages contain:
- Full pinout and hardware description
- Schematic
- Dimensions
- Arduino and ESP-IDF setup instructions
- Official demo / example code packages

---

## Key Interfaces for Our Project

From the board labeling:

| Label | Function | Use in LMYC Terminal |
|-------|----------|----------------------|
| 9 | **CAN header** | NMEA 2000 node (isolated transceiver still to fit) |
| 11 | **RS485 header** | NMEA 0183 |
| 13 | CAN terminal resistor selection | Enable/disable 120 Ω |
| 14 | RS485 terminal resistor selection | Enable/disable 120 Ω |
| 7 / 6 | UART2 / UART1 | One UART, to the CH343 Type-C (UART1) or the UART header (UART2), chosen by the UART select switch (item 15). **Keep it on UART1**: on UART2 the Type-C socket still enumerates but carries nothing (the likely cause of the old "does not deliver flash data" on the Mac). |
| 10 | I2C header | Sensors / expansion |
| 4 | TF card slot | Local logs, reference data |
| 5 | USB Type-C | ESP32 native USB. Shared with CAN: gone in NMEA 2000 mode. Flash port on a Mac (see below) |

### Which socket, by mode

The board is powered only through a USB-C socket.

| Mode | USB socket | UART socket |
|------|-----------|-------------|
| Simulator | Log + commands; flashing | Commands; log mirrored (firmware 0.9 and later) |
| NMEA 2000 | Pins are CAN. Nothing plugged in | The only console. Bus power (pigtail from the Pololu, [20](20-n2k-bus-power-supply.md)), or a battery-only laptop instead |

`python3 tools/board.py status` lists both sockets and the mode. **Isolation:** the board is not isolated from the bus. While the terminal is on the backbone, never plug a laptop that is on mains or ship power (or charging) into the UART socket: battery only, charger unplugged, nothing else connected to the laptop.

### Flashing from a Mac

Two Type-C sockets. The one marked **UART** is the CH343 (`1A86:55D3`). It shows up under `/dev/cu.usbmodem…` and `make flash` will select it, then esptool reports `No serial data received`. Holding BOOT and tapping RESET on that socket did not change that.

The socket marked **USB** is the ESP32's own USB and is shared with the CAN connector, so a normal plug-in creates no serial port. Unplug that cable, hold **BOOT**, plug it in, release **BOOT**. The port comes up as "USB JTAG/serial debug unit" (`303A:1001`). On 2026-10-01 that was `/dev/cu.usbmodem101`, and `pio run -t upload --upload-port /dev/cu.usbmodem101` from `firmware/` wrote the image and verified the hash. The chip reported ESP32-S3 revision v0.2, 8 MB PSRAM, and 16 MB flash. The steps live in `firmware/README.md`.

On that same day the picture walked off the glass and the chip rebooted while LVGL draw buffers were in internal RAM and LVGL ran on its own task. The screen stayed up after those buffers moved back to PSRAM (40 rows) and `loop()` called the LVGL handler on the core that started the panel, with the UI drawn before Wi-Fi. Panel timing stays on the Waveshare defaults. Details are in `firmware/README.md`.

**Important safety note**  
The onboard CAN and RS485 transceivers are **not galvanically isolated**.  
The firmware makes the terminal a standard NMEA 2000 node, but the library author's certification checklist requires the device to be electrically isolated from the network. That can't be fixed in firmware. The fix is an isolated CAN transceiver (TI ISO1042 / ISO1050 class, or ADI ADM3053, which also isolates power) with isolated power for the bus side, in place of the onboard TJA1051. The planned Pololu 12 V to 5 V supply is not isolated either.  
Until then the board goes on a backbone only bus-powered with a single ground ([19](19-n2k-backbone-install.md#41-primary-powered-from-the-bus)). Never connect anything else that shares ground (laptop on mains, phone charger, USB) while it is on the backbone.  
The manufacturer and product codes are also not NMEA-issued yet (open-source manufacturer code 2046), so the terminal is not certified. See [05](05-nmea-connectivity.md#hardware-compliance-open-items).

---

## Getting Started – Recommended Path

### 1. Download official demos

From the Waveshare wiki / docs pages above, download the official example package (Arduino + ESP-IDF).

### 2. First bring-up (Arduino is easiest)

Recommended order of tests:

1. Basic display + touch
2. LVGL demo (proves the UI stack)
3. CAN transmit / receive example
4. RS485 example
5. TF card read/write

### 3. Development environment tips

**Arduino IDE typical settings for this board:**
- Board: ESP32S3 Dev Module (or Waveshare-specific if available)
- Flash Size: 8 MB (or 16 MB depending on exact module variant)
- PSRAM: OPI PSRAM / Enabled
- USB CDC On Boot: Enabled (for serial monitor)
- Upload: the Type-C socket marked USB, with BOOT held while the cable is plugged in (see Flashing from a Mac above). The UART socket's CH343 port enumerates and does not accept the upload on this Mac.

**ESP-IDF** is also fully supported and may be preferred later for more control over the NMEA and BLE stacks.

### 4. Community resources

- ESPHome package for this board: https://github.com/inytar/waveshare-esp32-s3-touch-lcd-7-esphome
- Several PlatformIO + LVGL example repositories exist (search “ESP32-S3-Touch-LCD-7”)

---

## Next Hardware Steps (after board arrives)

1. Confirm display + touch + LVGL demo runs
2. Confirm CAN and RS485 examples work with a USB-CAN / USB-RS485 adapter
3. Design a simple 12 V → 5 V power solution for cabin use
4. Begin QR-code pairing experiments (BLE)
5. Start the first logbook / instrument screen layout in LVGL

---

## Relation to Final Design

This board is a **development prototype**, not the final production unit.

We still plan to:
- Add an isolated CAN transceiver with isolated bus-side power, so the NMEA 2000 node is fully compliant
- Get NMEA-issued manufacturer and product codes, and decide on NMEA certification
- Provide a clean 12 V power path
- Fit the unit into a suitable cabin enclosure. A candidate is the one-piece, wall-mount [Printables case](https://www.printables.com/model/1558735-waveshare-esp32-s3-touch-lcd-7-case) for the original ESP32-S3-Touch-LCD-7 (not the 7B). It covers the display board only; openings for the N2K drop / CAN entry and the USB-C lead are drilled after printing and sealed with Sikaflex-291.
- Keep the software architecture independent of the exact display size so we can move to other boards later if needed

---

*Document created after hardware order – August 2026*

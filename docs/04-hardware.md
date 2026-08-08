# 04 – Hardware

## Recommended Boat Terminal

### Primary Candidate: Waveshare ESP32-S3-Touch-LCD-4.3B

| Feature | Specification | Why it matters |
|---------|---------------|----------------|
| Display | 4.3" 800×480 capacitive touch | Readable in cabin, good enough for gauges + menus |
| Processor | ESP32-S3 dual-core 240 MHz | Sufficient for LVGL UI + NMEA parsing |
| Memory | 16 MB Flash + 8 MB PSRAM | Comfortable for modern embedded GUI |
| Power | 7–36 V DC wide input | Direct connection to boat 12 V system |
| Interfaces | CAN, RS485, UART, isolated digital I/O | Native support for NMEA 2000 (CAN) and NMEA 0183 |
| Connectivity | Wi-Fi + Bluetooth 5 LE | Pairing and optional local network |
| Other | TF card, RTC, battery header | Logging and timekeeping |

Approximate cost (2026): **CAD $50–70** for the board itself.

### Alternative / Larger Options from Waveshare

| Model | Size | Notes |
|-------|------|-------|
| ESP32-S3-Touch-LCD-5 / 5B | 5" | Slightly more screen real estate |
| ESP32-S3-Touch-LCD-7 / 7B | 7" | Better for multiple gauges at once; still cabin-friendly |
| ESP32-S3-Touch-LCD-4 (square) | 4" 480×480 | Already used in several open marine projects |

**Recommendation for pilot:** Start with the **4.3B** or the **7"** version depending on available cabin space on the chosen pilot boat.

## Power & Installation

- Powered from the boat’s 12 V bus via a fused connection (inline fuse recommended)
- The wide 7–36 V input removes the need for a separate regulator in most cases
- Optional small LiPo or supercapacitor for clean shutdown and RTC backup
- Mounting: simple bulkhead or surface mount with a 3D-printed or off-the-shelf enclosure
- Cabin installation only for v1 (not weatherproof)

## Cabling for NMEA

- **NMEA 0183**: RS422 / RS485 or TTL UART → board RS485 or UART pins
- **NMEA 2000**: Standard Micro-C or DeviceNet connector → CAN transceiver already present on the board (or external transceiver if needed)
- Keep cable runs short and use proper shielding / twisted pair where possible

## Enclosure Considerations

- Not waterproof — cabin use only
- Protect against accidental knocks and condensation
- Provide a clean way to route power and NMEA cables
- Optional simple sunshade or matte film if glare is an issue

## Bill of Materials (Pilot Estimate per Boat)

| Item | Approx. Cost (CAD) |
|------|--------------------|
| Waveshare ESP32-S3 terminal | 55–80 |
| Enclosure + mounting hardware | 15–30 |
| Power cable + fuse + connectors | 10–20 |
| NMEA interface cable / adapter | 15–40 |
| Miscellaneous (strain relief, labels) | 10 |
| **Total per boat** | **≈ $100–170** |

Costs drop further if the club buys a small batch.

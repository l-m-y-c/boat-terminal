# 04 – Hardware

## Recommended Boat Terminal

### Primary Candidate: Waveshare ESP32-S3-Touch-LCD-4.3B

| Feature | Specification | Why it matters |
|---------|---------------|----------------|
| Display | 4.3" 800×480 capacitive touch | Readable in cabin, good enough for gauges + menus |
| Processor | ESP32-S3 dual-core 240 MHz | Sufficient for LVGL UI + NMEA parsing |
| Memory | 16 MB Flash + 8 MB PSRAM | Comfortable for modern embedded GUI |
| Power | 7–36 V DC wide input | Direct connection to boat 12 V system |
| Interfaces | CAN, RS485, UART, isolated digital I/O | Base for NMEA 0183 and (with proper external transceiver) NMEA 2000 |
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

## Critical Hardware Requirement: Isolated NMEA 2000 Interface

The ESP32 board’s onboard CAN controller is **not** sufficient by itself for safe connection to a boat’s NMEA 2000 backbone.

**Required for any NMEA 2000 installation:**

- A proper **galvanically isolated** NMEA 2000 / CAN transceiver (e.g. ISO1050, or a purpose-built marine NMEA 2000 interface module)
- Correct Load Equivalency Number (LEN) budgeting
- Use of a certified or high-quality Micro-C drop cable / tee rather than ad-hoc wiring
- Clear failure behaviour: if the terminal faults, it must drop off the bus cleanly and never load or disrupt the backbone

This is a safety and liability consideration, not merely an engineering preference. See [05 – NMEA Connectivity](05-nmea-connectivity.md) for the full requirements.

## Power & Installation

- Powered from the boat’s 12 V bus via a fused connection (inline fuse required)
- The wide 7–36 V input removes the need for a separate regulator in most cases
- Optional small LiPo or supercapacitor for clean shutdown and RTC backup
- Mounting: simple bulkhead or surface mount with a 3D-printed or off-the-shelf enclosure
- Cabin installation only for v1 (not weatherproof)

## Cabling Summary

| Standard | Recommended Approach |
|----------|----------------------|
| NMEA 0183 | Isolated RS-422 / RS-485 or opto-isolated UART |
| NMEA 2000 | Isolated transceiver + certified Micro-C drop cable |

Keep cable runs short. Use proper shielding and twisted pair where applicable.

## Enclosure Considerations

- Not waterproof — cabin use only
- Protect against accidental knocks and condensation
- Provide clean strain relief for power and data cables
- Optional matte film or light hood if glare is an issue

## Bill of Materials (Pilot Estimate per Boat)

| Item | Approx. Cost (CAD) |
|------|--------------------|
| Waveshare ESP32-S3 terminal | 55–80 |
| Isolated NMEA 2000 transceiver / interface | 40–90 |
| Enclosure + mounting hardware | 15–30 |
| Power cable + fuse + connectors | 10–20 |
| Certified NMEA drop cable / adapters | 25–60 |
| Miscellaneous (strain relief, labels, etc.) | 10–15 |
| **Hardware total per boat** | **≈ $155–295** |

Install and commissioning time is additional (see Costs & Rollout). Costs improve with a small batch purchase.

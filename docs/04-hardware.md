# 04 – Hardware

## Overview of the On-Boat Hardware Mix

The project now uses two complementary displays plus the member’s phone:

| Role | Hardware | Key characteristics |
|------|----------|---------------------|
| **Interactive cabin terminal** | Waveshare ESP32-S3-Touch-LCD-7 (ordered) | Fast colour capacitive touch, CAN + RS485, cabin use |
| **Persistent status / overview board** | 13.3" Spectra 6 E-Ink + XIAO EE02 | 1200×1600, 6-colour, excellent daylight, ultra-low power when static, battery capable |
| **Primary UI & compute** | Member’s phone | Rich maps, forms, photos, cellular |
| **Optional mesh experiments** | XIAO ESP32S3 + Wio-SX1262 | LoRa / Meshtastic |

## 1. Interactive Cabin Terminal (Primary)

**Ordered prototype:** Waveshare ESP32-S3-Touch-LCD-7

| Feature | Specification | Why it matters |
|---------|---------------|----------------|
| Display | 7" capacitive touch, colour | Readable interaction for logbook, instruments, menus |
| Processor | ESP32-S3 | Sufficient for LVGL UI + NMEA parsing |
| Interfaces | CAN header + RS485 header | Base for NMEA 0183 and NMEA 2000. The terminal is a standard NMEA 2000 node; full compliance needs an isolated CAN transceiver |
| Connectivity | Wi-Fi + Bluetooth 5 LE | Pairing and local network |
| Power | 5 V (requires proper 12 V → 5 V conversion) | Boat power via buck converter + fuse |

**Critical requirement:** On NMEA 2000 the terminal is a standard node (address claim, NAME, request handling, heartbeat, standard CAN fault confinement, LEN 10). It sends only network-management messages, never instrument data. See [05 – NMEA Connectivity](05-nmea-connectivity.md#the-terminal-as-an-nmea-2000-node).

**Open hardware items (not fixable in firmware):**

1. **Isolation.** A compliant N2K device is electrically isolated from the network. The board's onboard TJA1051 is not isolated, and the planned Pololu 12 V to 5 V supply is not isolated. Fix: an isolated CAN transceiver (TI ISO1042 / ISO1050 class, or ADI ADM3053, which also isolates power) with isolated power for the bus side, in place of the onboard TJA1051. Until then, never connect anything else that shares ground (laptop on mains, phone charger, USB) while it is on the backbone.
2. **NMEA codes and certification.** Manufacturer and product codes must be issued by NMEA (membership needed; about $1,200 and $450 at 2024 member prices, per the library author). Certification testing costs extra (his total estimate for a new member is about $4,000). Until then the terminal uses the open-source manufacturer code 2046 and is not certified.

Cabin installation for the interactive terminal (not weatherproof).

## 2. Persistent Status Board (13.3" E-Ink)

**Ordered hardware:**
- 13.3" Spectra™ 6 E-Ink panel (1200×1600, six colours)
- XIAO ePaper Display Board EE02 (ESP32-S3 Plus)

| Characteristic | Implication |
|----------------|-------------|
| Resolution 1200×1600 | Perfectly usable for charts and overview maps |
| Six colours (B/W/R/Y/G/Blue) | Sufficient for clear navigational rendering when the phone prepares the image |
| Full refresh typically 10–20 s | Not suitable for fluid interaction or live moving maps |
| Near-zero power while static | Excellent for always-on status |
| EE02 battery support (JST 2.0 mm) | Can run from a substantial lithium pack |
| Operating temperature –20 °C to 70 °C | Suitable for protected outdoor mounting |

**Intended use**
- Always-visible booking / occupancy status
- Open maintenance summary
- High-quality map or route overview pushed from the phone
- Key instrument summary

**Update model**
- Push on demand from the phone, **or**
- Periodic refresh every 30–60 seconds

At typical club speeds (5–7 kn) and the ranges shown on overview maps, this rate is practical and keeps the display useful without fighting the panel’s physics.

**Mounting options**
- Cabin
- Outside in a 3D-printed or commercial waterproof enclosure with clear front, large rechargeable lithium battery, and simple charging provision

The EE02 board already includes battery connector, charging support, and power switch, making a self-contained outdoor unit realistic.

## 3. Supporting Hardware

| Item | Role |
|------|------|
| XIAO Expansion Board (Grove) | Prototyping, small OLED, extra sensors |
| XIAO ESP32S3 + Wio-SX1262 | Optional LoRa / Meshtastic mesh experiments |

## Power Notes

| Device | Power approach |
|--------|----------------|
| 7" interactive terminal | Prototype: NMEA 2000 bus power → fused buck converter → 5 V (non-isolated, see [20](20-n2k-bus-power-supply.md)) |
| 13.3" E-Ink + EE02 | Boat 12 V (via converter) **or** large Li battery for fully independent outdoor mounting |
| Isolation | Required for a compliant NMEA 2000 connection (open item on the prototype); recommended on NMEA 0183 for permanent installs |

## Bill of Materials (Current Prototype Direction)

| Item | Approx. Cost |
|------|--------------|
| Waveshare ESP32-S3-Touch-LCD-7 | Already ordered |
| Isolated CAN transceiver and isolated bus-side power | 40–90 CAD |
| 13.3" Spectra 6 E-Ink panel | $149 USD |
| XIAO EE02 driver board | $14.90 USD |
| Enclosure + battery for E-Ink (optional outdoor) | TBD |
| XIAO Expansion + LoRa kit | Already ordered |

Exact per-boat production costing will be refined after the pilot.

## Enclosure Philosophy

- **7" terminal:** Simple cabin enclosure, protection from knocks and condensation. A candidate is the one-piece, wall-mount [Waveshare ESP32-S3-Touch-LCD-7 case](https://www.printables.com/model/1558735-waveshare-esp32-s3-touch-lcd-7-case) on Printables, for the original board (not the 7B). It covers the display board only. Openings for the N2K drop and USB-C lead are drilled after printing and sealed with Sikaflex-291. The N2K power parts need their own small box. See [19](19-n2k-backbone-install.md#2-parts-list) and [20](20-n2k-bus-power-supply.md).
- **13.3" E-Ink:** Can be a sealed, weather-resistant unit with large battery if mounted where it has clear view from the cockpit. E-ink’s readability in daylight and low static power make this attractive.

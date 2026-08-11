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
| Interfaces | CAN header + RS485 header | Base for NMEA 0183 and (with external isolation) NMEA 2000 |
| Connectivity | Wi-Fi + Bluetooth 5 LE | Pairing and local network |
| Power | 5 V (requires proper 12 V → 5 V conversion) | Boat power via buck converter + fuse |

**Critical requirement:** Any connection to a live NMEA 2000 backbone must use a proper galvanically isolated transceiver and clean failure behaviour. See [05 – NMEA Connectivity](05-nmea-connectivity.md).

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
| 7" interactive terminal | Boat 12 V → fused buck converter → 5 V |
| 13.3" E-Ink + EE02 | Boat 12 V (via converter) **or** large Li battery for fully independent outdoor mounting |
| Isolation | Required on any NMEA 2000 connection; recommended on NMEA 0183 for permanent installs |

## Bill of Materials (Current Prototype Direction)

| Item | Approx. Cost |
|------|--------------|
| Waveshare ESP32-S3-Touch-LCD-7 | Already ordered |
| Isolated NMEA interface components | 40–90 CAD |
| 13.3" Spectra 6 E-Ink panel | $149 USD |
| XIAO EE02 driver board | $14.90 USD |
| Enclosure + battery for E-Ink (optional outdoor) | TBD |
| XIAO Expansion + LoRa kit | Already ordered |

Exact per-boat production costing will be refined after the pilot.

## Enclosure Philosophy

- **7" terminal:** Simple cabin enclosure, protection from knocks and condensation.
- **13.3" E-Ink:** Can be a sealed, weather-resistant unit with large battery if mounted where it has clear view from the cockpit. E-ink’s readability in daylight and low static power make this attractive.

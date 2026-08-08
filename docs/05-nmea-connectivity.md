# 05 – NMEA Connectivity

## Supported Standards

The terminal is designed to accept both major marine data standards:

### NMEA 0183
- Serial protocol (typically 4800 or 38400 baud)
- Common on older instruments and many depth / wind / GPS units
- Connected via the board’s RS485 or UART interface
- Easy to parse with existing open-source libraries

### NMEA 2000
- CAN-bus based, modern standard
- Preferred on newer boats
- The Waveshare board includes a CAN interface
- Mature open-source library ecosystem (NMEA2000 by ttlappalainen and community forks)

## What Data Should Be Displayed?

**Minimum useful set for cabin terminal:**

- Depth (below transducer / below keel)
- Boat speed (through water or SOG)
- Wind speed & direction (apparent / true if available)
- Heading or COG
- GPS position (lat/lon) — mainly for confirmation
- Battery voltage / house bank status (if available on the network)
- Engine hours or RPM (if the boat has an engine network connection)

**Nice-to-have later:**
- Water temperature
- Air temperature / barometric pressure
- AIS targets (summary only)
- Autopilot status (read-only)

## Architecture Choices

### Option A – Direct NMEA to Terminal (Recommended for v1)
- Terminal talks directly to the boat’s NMEA network
- Simplest, lowest latency, works completely offline
- Phone only needs instrument data if the member wants to mirror it

### Option B – Signal K Bridge
- A small Signal K server (could even run on the same ESP32 or a separate Raspberry Pi) normalises all data
- Terminal becomes a Signal K consumer
- More flexible for future expansion, slightly more complex

**Proposal:** Start with **Option A** (direct NMEA).  
Add Signal K later if the club wants richer integration or multiple displays.

## Physical Connection Notes

- Most club boats already have some form of instrument network
- Installation will require identifying the existing NMEA 0183 talker or NMEA 2000 backbone
- A simple drop cable or tee connector is usually sufficient
- Isolation and proper termination remain important on NMEA 2000

## Library Support (for later implementation)

- NMEA 0183: multiple mature Arduino / ESP-IDF parsers
- NMEA 2000: `NMEA2000` + `NMEA2000_esp32` libraries are production-proven on ESP32
- Several existing open projects already run NMEA 2000 instrument displays on Waveshare ESP32-S3 boards

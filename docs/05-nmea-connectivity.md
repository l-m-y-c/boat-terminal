# 05 – NMEA Connectivity

## Supported Standards

The terminal is designed to accept both major marine data standards:

### NMEA 0183
- Serial protocol (typically 4800 or 38400 baud)
- Common on older instruments and many depth / wind / GPS units
- Connected via isolated RS-422 / RS-485 or opto-isolated UART
- Relatively low risk to the rest of the boat’s systems

### NMEA 2000
- CAN-bus based, modern standard
- Preferred on newer boats
- Requires careful, isolated connection (see safety section below)
- Mature open-source library ecosystem exists (`NMEA2000` + community ESP32 ports)

## What Data Should Be Displayed?

**Minimum useful set for cabin terminal:**

- Depth (below transducer / below keel)
- Boat speed (through water or SOG)
- Wind speed & direction (apparent / true if available)
- Heading or COG
- GPS position (lat/lon) — mainly for confirmation
- Battery voltage / house bank status (if available)
- Engine hours or RPM (if present on the network)

**Nice-to-have later:**
- Water temperature
- Air temperature / barometric pressure
- AIS targets (summary only)
- Autopilot status (read-only)

## Architecture Choices

### Option A – Direct NMEA to Terminal (Recommended for v1)
- Terminal talks directly to the boat’s NMEA network
- Lowest latency, works completely offline
- Simplest software path

### Option B – Signal K Bridge
- Intermediate Signal K server normalises data
- More flexible for future multi-display or richer integrations
- Adds complexity and another component to maintain

**Proposal:** Start with **Option A**. Revisit Signal K only if the pilot demonstrates clear need.

---

## NMEA 2000 Bus Safety Requirements (Critical)

Connecting any new device to a boat’s NMEA 2000 backbone is the highest-risk part of this project. The backbone often carries data used by the autopilot, chartplotter, and engine systems. A poorly designed node can degrade or disable the entire segment.

### Mandatory Requirements Before Any NMEA 2000 Connection

1. **Galvanic isolation**  
   The terminal must not share a common ground or power reference with the NMEA 2000 bus. An isolated CAN transceiver (or a purpose-built marine NMEA 2000 interface module) is required. The ESP32’s onboard CAN controller alone is insufficient.

2. **Clean failure mode**  
   If the terminal loses power, locks up, or is removed, it must drop off the bus cleanly. It must never load the bus, inject noise, or hold the bus in a dominant state.

3. **Load Equivalency Number (LEN)**  
   The interface must declare and respect a realistic LEN. The total LEN on any segment must stay within NMEA 2000 limits.

4. **Certified or high-quality drop cable**  
   Prefer a proper Micro-C drop cable and tee rather than DIY wiring. This reduces installation errors and improves reliability.

5. **No bus power draw from the backbone**  
   The terminal should be powered from the boat’s 12 V system, not from the NMEA 2000 net power pair, unless a carefully designed and approved interface is used.

### Liability Note

Modifying a vessel’s instrument network has insurance and liability implications. Before any permanent installation on a club boat, the Board should be informed and, if appropriate, the club’s insurer consulted. This is not purely a technical decision.

### Pilot Recommendation

For the first pilot boat, prefer a vessel that already has clean NMEA 0183 outputs or a well-documented NMEA 2000 backbone with available drop points. Avoid boats where the network is poorly understood or heavily loaded until the isolated interface has been proven.

---

## Physical Connection Notes

- Installation requires identifying the existing talker (0183) or backbone (2000)
- Keep runs short
- Use proper shielding and strain relief
- Document the connection point and any LEN contribution for future maintainers

## Library Support (for later implementation)

- NMEA 0183: multiple mature parsers exist for ESP-IDF / Arduino
- NMEA 2000: `NMEA2000` library family is the de-facto open standard on ESP32
- Several existing open projects already drive instrument displays on Waveshare ESP32-S3 hardware

# 16 – Related Projects: Tritium Analysis

Deep dive into [tritium.co.uk](https://tritium.co.uk) marine electronics projects  
(as of August 2026). This document captures findings relevant to the LMYC Boat Terminal.

---

## Summary

Tritium publishes simple, low-cost DIY marine electronics projects focused on **NMEA 0183**, **AIS**, and **ESP32** touchscreen displays.  
All projects emphasise:

- Readily available off-the-shelf parts
- Free firmware that can be flashed directly from a browser (Web Serial)
- No coding required for end users
- Browser-based configuration after first boot

**Important:** The firmware source code is **not published**.  
Only binaries + parts lists + wiring guides are provided.

---

## Projects

### 1. NMEA 0183 to WiFi Bridge

| Item | Detail |
|------|--------|
| **Purpose** | Bridge existing NMEA 0183 instruments to boat WiFi so phones/tablets can display GPS, AIS, depth, wind, speed |
| **Hardware** | ESP32-WROOM-32 + MAX3232 (HW-027) RS-232/TTL adapter + 8–30 V → 3.3 V buck converter |
| **Cost** | ~£15 |
| **Data path** | NMEA 0183 serial → ESP32 → UDP (port 2000) / TCP (port 10110) over WiFi |
| **Config** | Creates AP `NMEA_Bridge` / password `Epoxy123` → configure at `192.168.4.1` |
| **Source** | Closed (binary only) |

**Key practical notes from their support page:**
- Default baud often 38400 (some older instruments still use 4800)
- Instrument Tx → Bridge Rx
- Quality data-capable USB cable required for flashing
- Hold BOOT button if device does not enter flash mode automatically

### 2. NMEATouch20

| Item | Detail |
|------|--------|
| **Purpose** | Standalone touchscreen NMEA 0183 + AIS instrument display |
| **Hardware** | Guition ESP32-S3 4848S040 (4" 480×480 IPS, GT911 touch, 8 MB PSRAM, 16 MB Flash) |
| **Cost** | ~£20–25 |
| **Features** | SOG / COG with history graph, AIS target table with CPA/TCPA, depth, trip log, encounter logging to SD, basic race start tools |
| **Connectivity** | Joins boat WiFi or creates own hotspot; receives NMEA/AIS over UDP |
| **UI** | LVGL-based multi-screen swipe interface |
| **Source** | Closed (binary only) |

**Notable design choices:**
- AIS CPA adjusted for antenna vs hull position (ahead/astern clearance)
- Colour highlighting for close approaches and turning Class A vessels
- SD card required for persistent calibration and encounter history
- Not waterproof / not sunlight-readable (cabin use only)

### 3. Network Analyser

| Item | Detail |
|------|--------|
| **Purpose** | On-boat WiFi + NMEA diagnostics tool (no laptop needed) |
| **Hardware** | Same Guition ESP32-S3 4848S040 |
| **Features** | ARP scan, port scan, live NMEA sentence decoder (colour-coded), UDP broadcast verification, RSSI, basic spectrum view |
| **Source** | Closed (binary only) |

---

## Libraries used by Tritium (from their licences page)

| Library | Role |
|---------|------|
| **LVGL 9.2.2** | Primary UI framework |
| Arduino GFX Library | Display rendering support |
| TAMC GT911 | Capacitive touch driver |
| ESPAsyncWebServer + AsyncTCP | Browser configuration interface |
| ArduinoJson | Settings / data handling |
| LittleFS | Filesystem (settings + SD logging) |
| ArduinoOTA | Over-the-air updates (optional) |
| FreeRTOS | Task scheduling |
| ESP32Ping | Network diagnostics |

This confirms that **LVGL** is a proven choice for polished ESP32-S3 instrument UIs.

---

## Hardware notes – Guition ESP32-S3 4848S040

- 4.0" 480×480 IPS (ST7701 RGB interface)
- ESP32-S3, 8 MB PSRAM, 16 MB Flash
- GT911 capacitive touch
- USB-C
- Optional microSD
- Popular in Home Assistant / ESPHome wall-panel community
- **No free GPIO left** after display + touch + SD (important limitation)
- Not marine-rated (indoor / cabin only)

Useful community resources for this board (not Tritium):
- [ESPHome device page](https://devices.esphome.io/)
- Various GitHub repos for ST7701 + LVGL init sequences

---

## Transferable lessons for LMYC Boat Terminal

### Strong positive patterns

1. **Browser-based firmware install + AP configuration**  
   Extremely low friction for club volunteers. We should aim for the same experience.

2. **WiFi NMEA as a first-class input**  
   UDP 2000 / TCP 10110 is a de-facto standard. Supporting this *in addition to* hard-wired RS485/CAN makes the terminal far more flexible across the fleet.

3. **LVGL for the UI**  
   Confirmed as a practical choice for multi-screen instrument displays on ESP32-S3.

4. **Simple dual-mode networking**  
   Join existing boat WiFi **or** create own hotspot. Useful for initial setup and for boats without a permanent WiFi network.

5. **SD card for persistent data**  
   Calibration, logs, encounter history. We already planned local storage; this reinforces the value.

### What we should *not* copy

- Closed-source firmware model — our project remains fully open.
- Single-purpose instrument focus — our terminal is also a co-op tool (logbook, maintenance, booking pairing).
- Lack of isolation discussion for permanent installs — we keep the stronger safety stance already documented.

### Gaps relative to LMYC goals

Tritium projects do **not** address:
- Multi-user / member pairing
- Integration with an existing club booking system
- Digital logbook or maintenance reporting
- QR-code based check-in
- Fleet-wide consistency across many boats

These remain our primary differentiators.

---

## Recommended actions

| Priority | Action |
|----------|--------|
| High | Support **both** hard-wired NMEA (RS485/CAN) **and** WiFi NMEA (UDP/TCP) on the terminal |
| High | Target a “flash from browser + simple config page” firmware update experience |
| Medium | Use LVGL for the UI (already under consideration) |
| Medium | Study their AIS CPA presentation if we decide to show AIS data |
| Low | Consider the Guition 4848S040 only as a possible smaller/cheaper secondary display, not as a replacement for the 7" prototype |

---

## References

- [Tritium Projects](https://tritium.co.uk/projects)
- [NMEA 0183 to WiFi Bridge](https://tritium.co.uk/projects/nmea-wifi-bridge)
- [NMEATouch20](https://tritium.co.uk/projects/nmea-touch-20)
- [Network Analyser](https://tritium.co.uk/projects/network-analyser)
- [Third-party licences](https://tritium.co.uk/licences)
- [Support / troubleshooting](https://tritium.co.uk/support)

---

*Document created from public information only. Tritium firmware remains closed-source; no code was reused.*

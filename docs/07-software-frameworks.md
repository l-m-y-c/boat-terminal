# 07 – Software Frameworks

## Boat Terminal (ESP32-S3)

### Recommended Stack

| Layer | Technology | Reason |
|-------|------------|--------|
| Framework | ESP-IDF (Espressif) or Arduino-ESP32 | Mature, excellent hardware support |
| UI | LVGL | Industry standard for embedded touch GUIs, good performance on ESP32-S3 |
| NMEA 2000 | NMEA2000 + NMEA2000_esp32 libraries | Most widely used open implementation |
| NMEA 0183 | Existing community parsers | Simple and reliable |
| Connectivity | ESP-IDF BLE + Wi-Fi | Native support |
| Storage | LittleFS or SPIFFS + optional TF card | Configuration and short logs |

### Why LVGL?

- Designed for resource-constrained devices
- Excellent touch support
- Active community and many marine/embedded examples
- Allows clean gauge and menu designs without a full operating system

### Alternative Approaches

- Pure Arduino + simpler graphics libraries (faster to prototype, less polished)
- MicroPython / CircuitPython (attractive for rapid development, slightly higher overhead)

**Proposal:** Prototype in Arduino or ESP-IDF + LVGL.  
Final decision can be made after the first working NMEA display.

## Phone Application

### Options

| Approach | Pros | Cons | Fit for LMYC |
|----------|------|------|--------------|
| **Progressive Web App (PWA)** | No app-store approval, easy updates, works on iOS & Android | Some BLE limitations on iOS, less “native” feel | Strong candidate |
| Flutter | Excellent cross-platform, good BLE support, single codebase | Requires Flutter knowledge | Very good |
| React Native | Large ecosystem, good BLE libraries | Slightly more complex tooling | Good |
| Native (Swift + Kotlin) | Best platform integration | Two codebases, higher effort | Overkill for v1 |

### Recommendation

**Start with a Progressive Web App** if BLE pairing can be made reliable enough,  
or **Flutter** if a more native experience and robust Bluetooth are desired.

Both keep the door open for later native features.

## Club Backend

- Prefer extending the existing LMYC booking system rather than building a parallel one
- Minimal new API endpoints needed:
  - Issue short-lived pairing token for a booking
  - Accept check-in / check-out events
  - Accept issue reports and engine-hour updates
- Authentication can reuse existing member credentials

## Development Principles

- Open source from day one (this repository)
- Prefer well-maintained libraries over reinventing protocols
- Keep the terminal firmware small and auditable
- Make the phone app progressive — useful even before full terminal integration

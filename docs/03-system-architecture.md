# 03 – System Architecture

## High-Level View

```
┌─────────────────────┐
│   Member Phone      │  ← Primary UI & powerhouse
│                     │
│ • LMYC Companion     │
│ • Charts / Weather  │
│ • Booking / Photos  │
│ • Cellular backhaul │
└─────────┬───────────┘
          │ QR + BLE / Wi-Fi
          │ (pairing + image push)
          ▼
┌─────────────────────┐         ┌──────────────────────────┐
│  Interactive        │         │  Persistent Status Board │
│  Cabin Terminal     │         │  (13.3" Spectra 6 E-Ink) │
│  (7" colour touch)  │         │                          │
│                     │         │ • Always-on / low power  │
│ • Logbook & maint.  │         │ • Daylight readable      │
│ • Boat reference    │         │ • Map / route overview   │
│ • Live instruments  │         │ • Booking & status       │
│ • QR pairing        │         │ • Battery capable        │
└─────────┬───────────┘         └────────────┬─────────────┘
          │                                  │
          │ NMEA 0183 / 2000 (isolated)      │ Wi-Fi image push
          ▼                                  │ (on demand or 30–60 s)
┌─────────────────────┐                      │
│  Boat Instruments   │                      │
│  + Autopilot data   │                      │
└─────────────────────┘                      │
                                             ▼
                                   (optional outdoor
                                    waterproof enclosure
                                    + large Li battery)
```

## Four Main Components

### 1. Member Phone (primary UI)

- Runs the LMYC companion (app or progressive web app)
- Handles authentication against the club booking system
- Performs the secure QR-initiated pairing ceremony
- Provides cellular connectivity for log upload and issue reporting
- Remains the rich interface for full charts, weather, photos, and detailed forms
- Can push rendered map / status images to the E-Ink board

### 2. Interactive Cabin Terminal (7" colour touch)

- Waveshare ESP32-S3 capacitive touch display (or equivalent)
- Fast enough for real interaction
- Primary on-boat roles:
  - QR check-in / pairing surface
  - Digital logbook entry
  - Maintenance problem reporting
  - Live instrument pages
  - Quick boat-specific reference
- Powered from boat 12 V (via suitable regulation)
- Reads NMEA via isolated interfaces

### 3. Persistent Status Board (13.3" Spectra 6 E-Ink)

- Large, high-resolution (1200×1600), six-colour e-paper
- Excellent daylight readability and near-zero power while static
- Driven by XIAO EE02 (ESP32-S3)
- Roles:
  - Always-visible booking / occupancy status
  - Open maintenance summary
  - High-quality map or route overview (pushed from phone)
  - Key instrument summary
- Update model: **push on demand** or **periodic (30–60 s)**  
  At typical club boat speeds (5–7 kn) and the ranges shown on overview maps, this is entirely practical.
- Can be mounted in the cabin or in a 3D-printed / sealed outdoor enclosure with a substantial lithium battery (EE02 supports battery operation)

### 4. Club Backend

- Existing or lightly extended LMYC booking / membership system
- Issues short-lived pairing tokens when a booking becomes active
- Receives check-in, check-out, engine hours, and issue reports
- Source of truth for “who currently has this boat”

## Design Principles

1. **Phone is the powerhouse and primary UI** – maps, accounts, photos, complex interaction
2. **Serve the co-operative first** – logbook, maintenance, boat knowledge, clean handovers
3. **Two complementary on-boat displays**:
   - Fast colour touch for interaction
   - Large low-power E-Ink for persistent, glanceable information
4. **Pairing is temporary** – tied to the booking window (QR is the preferred bootstrap)
5. **Offline-first and pairing-independent for instruments** – live NMEA display continues even if the token expires
6. **Bus safety first** – any NMEA 2000 connection uses a galvanically isolated interface with clean failure behaviour
7. **Professional tools remain primary for navigation** – neither display replaces a chartplotter or controls the autopilot
8. **Open and inspectable** – more than one person must be able to maintain it

## Data Flow Summary

| Data | Source | Destination | Path |
|------|--------|-------------|------|
| Depth, wind, speed, battery, etc. | Boat instruments | 7" terminal | NMEA direct (isolated) |
| Waypoint / autopilot status | Existing plotter / autopilot | 7" terminal (awareness) | NMEA listen |
| Booking status | Club backend | Both displays + Phone | Via phone after QR pairing |
| Log entries & issues | Phone or 7" terminal | Club backend | Cellular |
| Map / route overview images | Phone | 13.3" E-Ink | Wi-Fi push (on demand or 30–60 s) |
| Pairing token | Club backend | Phone → 7" terminal | QR + BLE (LE Secure Connections) |

## Why the Dual-Display Split Works

- The **7" colour touch** gives the interaction speed people expect for check-in, logbook, and live instruments.
- The **13.3" E-Ink** gives a large, sunlight-readable surface that can hold useful information (including map context) for long periods while drawing almost no power.
- The **phone** keeps the expensive, power-hungry, complex work.
- Update rates of 30–60 seconds (or pure on-demand) are well matched to the speeds and ranges typical of club sailing.

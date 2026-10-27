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

### 4. Club Backend (lmyc.ca)

- WordPress and MemberPress stay the membership system. The phone logs in with OAuth 2.0 authorization code + PKCE against those same users. The website keeps its cookie login.
- The terminal is a device bound to one sailboat id. It is not an OAuth client and it does not store member tokens.
- The TF card on the Waveshare is this boat's copy of the maintenance book, photos included. lmyc.ca is the club's copy. The phone carries changes between them.
- Details, field names, and the build order are in [18 – Maintenance handoff](18-maintenance-handoff-and-auth.md).

### 5. E-Ink board

The 13.3″ panel is a separate project. It displays pictures the phone sends. It is not part of login, pairing, or the maintenance book.

## Design Principles

1. **Phone is the powerhouse and primary UI** – maps, accounts, photos, complex interaction
2. **Serve the co-operative first** – logbook, maintenance, boat knowledge, clean handovers
3. **The 7″ is the boat's interactive terminal.** The large E-Ink is a separate project that shows pictures the phone sends.
4. **Pairing is temporary** – tied to the booking window (QR is the preferred bootstrap)
5. **Offline-first and pairing-independent for instruments** – live NMEA display continues even if the token expires
6. **Bus safety first** – on NMEA 2000 the terminal is a standard node (address claim, NAME, request handling, heartbeat, standard CAN fault confinement) that sends no instrument data, with clean failure behaviour. A compliant connection uses a galvanically isolated interface; on the prototype that is still an open item ([05](05-nmea-connectivity.md#hardware-compliance-open-items))
7. **Professional tools remain primary for navigation** – neither display replaces a chartplotter or controls the autopilot
8. **Open and inspectable** – more than one person must be able to maintain it

## Data Flow Summary

| Data | Source | Destination | Path |
|------|--------|-------------|------|
| Depth, wind, speed, battery, etc. | Boat instruments | 7" terminal | NMEA direct (isolated) |
| Waypoint / autopilot status | Existing plotter / autopilot | 7" terminal (awareness) | NMEA listen |
| Booking status | lmyc.ca | Phone, and the 7″ while paired | Via the phone after pairing |
| This boat's maintenance book, including photos | TF card on the 7″ | Next member, on screen or via phone on the SoftAP | Local. No cell required |
| Maintenance edits made at home | lmyc.ca | TF card | Phone courier, when it has a signal |
| New reports filed on board | Phone or 7″, then the TF card | lmyc.ca | Phone courier, bearer token |
| Map / route overview images | Phone | 13.3" E-Ink | Separate project. Wi-Fi image push |
| Boarding pass | lmyc.ca, fetched while the phone has coverage | 7″ terminal | QR + BLE. Checked locally |

## Why the split works

- The **phone** keeps accounts, photos, charts, and the network path to lmyc.ca.
- The **7" colour touch** is on the boat: instruments, the maintenance book, and pairing. Its TF card holds the book when nobody has a signal.
- The **13.3" E-Ink**, as a separate project, can hold a picture the phone pushed (a route overview, a status board) and draw almost nothing while that picture sits there.

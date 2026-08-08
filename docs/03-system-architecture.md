# 03 – System Architecture

## High-Level View

```
┌─────────────────────┐       QR + BLE / Wi-Fi       ┌──────────────────────┐
│   Member Phone      │◄────────────────────────────►│   Boat Terminal      │
│                     │                              │  (ESP32 + Touch)     │
│ • LMYC Companion    │                              │                      │
│ • Charts / Weather  │                              │ • Logbook & maint.   │
│ • Booking status    │                              │ • Boat reference     │
│ • Cellular backhaul │                              │ • Live instruments   │
│ • Photos / Log      │                              │ • Basic nav awareness│
└─────────┬───────────┘                              └──────────┬───────────┘
          │                                                     │
          │ HTTPS (when online)                                 │ NMEA 0183 / 2000
          ▼                                                     ▼               (isolated)
┌─────────────────────┐                              ┌──────────────────────┐
│  LMYC Club Backend  │                              │  Boat Instruments    │
│  (Booking system)   │                              │  + Autopilot data    │
└─────────────────────┘                              └──────────────────────┘
```

## Three Main Components

### 1. Boat Terminal (always present)

- Small capacitive touchscreen running custom firmware
- Powered from boat 12 V system
- Reads NMEA data via an **isolated** interface
- Primary roles: digital logbook, maintenance visibility, boat reference, instrument display
- Secondary role: limited navigation awareness (listens to waypoint / autopilot data)
- Acts as a Bluetooth LE peripheral and displays a QR code for pairing

### 2. Member Phone (comes and goes)

- Runs the LMYC companion (app or progressive web app)
- Handles authentication against the club booking system
- Performs the secure QR-initiated pairing ceremony
- Provides cellular connectivity for log upload and issue reporting
- Remains the rich interface for full charts, weather, and photos

### 3. Club Backend

- Existing or lightly extended LMYC booking / membership system
- Issues short-lived pairing tokens when a booking becomes active
- Receives check-in, check-out, engine hours, and issue reports
- Provides the source of truth for “who currently has this boat”

## Design Principles

1. **Serve the co-operative first** – logbook, maintenance, boat knowledge, clean handovers
2. **Phone does the heavy lifting** – maps, accounts, photos, cellular
3. **Terminal stays simple and robust** – always-available local companion
4. **Pairing is temporary** – tied to the booking window (QR is the preferred bootstrap)
5. **Offline-first and pairing-independent for instruments** – live NMEA display continues even if the token expires
6. **Bus safety first** – any NMEA 2000 connection uses a galvanically isolated interface with clean failure behaviour
7. **Mutual authentication** – both the booking token and the physical terminal are verified
8. **Professional tools remain primary for navigation** – the terminal may listen and display awareness data but does not replace a chartplotter or control the autopilot
9. **Open and inspectable** – more than one person must be able to maintain it

## Data Flow Summary

| Data | Source | Destination | Path |
|------|--------|-------------|------|
| Depth, wind, speed, battery, etc. | Boat instruments | Terminal screen | NMEA direct (isolated) |
| Waypoint / autopilot status | Existing plotter / autopilot | Terminal (awareness only) | NMEA listen |
| Booking status | Club backend | Terminal + Phone | Via phone after QR pairing |
| Log entries & issues | Phone or Terminal | Club backend | Cellular |
| Pairing token | Club backend | Phone → Terminal | QR + BLE (LE Secure Connections) |

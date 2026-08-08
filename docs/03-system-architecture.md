# 03 – System Architecture

## High-Level View

```
┌─────────────────────┐       BLE / Local Wi-Fi      ┌──────────────────────┐
│   Member Phone      │◄────────────────────────────►│   Boat Terminal      │
│                     │                              │  (ESP32 + Touch)     │
│ • LMYC App / PWA    │                              │                      │
│ • Charts / Weather  │                              │ • Live instruments   │
│ • Booking status    │                              │ • Booking status     │
│ • Cellular backhaul │                              │ • Simple menus       │
│ • Photos / Log      │                              │ • NMEA display       │
└─────────┬───────────┘                              └──────────┬───────────┘
          │                                                     │
          │ HTTPS (when online)                                 │ NMEA 0183 / 2000
          ▼                                                     ▼
┌─────────────────────┐                              ┌──────────────────────┐
│  LMYC Club Backend  │                              │  Boat Instruments    │
│  (Booking system)   │                              │  (existing)          │
└─────────────────────┘                              └──────────────────────┘
```

## Three Main Components

### 1. Boat Terminal (always present)

- Small capacitive touchscreen running custom firmware
- Powered from boat 12 V system
- Reads NMEA data via an **isolated** interface
- Shows gauges, status, and pairing state
- Acts as a Bluetooth LE peripheral (and optionally a local Wi-Fi access point)

### 2. Member Phone (comes and goes)

- Runs the LMYC companion app (or progressive web app)
- Handles authentication against the club booking system
- Performs the secure pairing ceremony with the terminal
- Provides cellular connectivity for check-in confirmation, log upload, and issue reporting
- Can display richer information (charts, weather, manuals) that would be impractical on the small terminal

### 3. Club Backend

- Existing or lightly extended LMYC booking / membership system
- Issues short-lived pairing tokens when a booking becomes active
- Receives check-in, check-out, engine hours, and issue reports
- Provides the source of truth for “who currently has this boat”

## Design Principles

1. **Phone does the heavy lifting** – maps, accounts, photos, cellular
2. **Terminal stays simple and robust** – instruments + status + pairing
3. **Pairing is temporary** – tied to the booking window
4. **Offline-first and pairing-independent for instruments** – live NMEA display must continue even if the token expires, the phone is lost, or the backend is unreachable
5. **Bus safety first** – any NMEA 2000 connection uses a galvanically isolated interface with clean failure behaviour
6. **Mutual authentication** – both the booking token and the physical terminal are verified
7. **Open and inspectable** – no black-box proprietary lock-in; more than one person must be able to maintain it

## Data Flow Summary

| Data | Source | Destination | Path |
|------|--------|-------------|------|
| Depth, wind, speed, etc. | Boat instruments | Terminal screen | NMEA direct (isolated) |
| Booking status | Club backend | Terminal + Phone | Via phone after pairing |
| Check-in / check-out | Phone | Club backend | Cellular |
| Engine hours / issues | Terminal or Phone | Club backend | Via phone |
| Pairing token | Club backend | Phone → Terminal | BLE (LE Secure Connections) |

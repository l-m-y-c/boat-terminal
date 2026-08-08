# 13 – Risks & Mitigations

This document captures the more serious risks identified during review and the planned mitigations. It is intended for the Board and technical volunteers.

## 1. NMEA 2000 Bus Integrity (High)

**Risk:** A poorly designed or failed terminal could degrade or disable the NMEA 2000 backbone, affecting autopilot, chartplotter, or engine data.

**Mitigation:**
- Mandatory galvanically isolated transceiver
- Clean drop-off failure mode required by design
- Certified or high-quality Micro-C drop cables preferred
- LEN budgeting documented per installation
- Board informed before any permanent modification; insurer consulted if appropriate
- Pilot prefers boats with well-understood networks

## 2. BLE Spoofing / Man-in-the-Middle (Medium–High)

**Risk:** A malicious device in a marina advertises as a club boat and captures a member’s pairing attempt.

**Mitigation:**
- Mutual authentication (token + terminal device identity)
- LE Secure Connections with Out-of-Band data from the terminal QR code
- Terminal identity provisioned at install time and verifiable by the phone/backend

## 3. Occupancy Information Leakage (Medium)

**Risk:** Passive BLE scanning reveals which boats are currently unoccupied.

**Mitigation:**
- Minimal advertising payload
- Booking / occupancy state revealed only after authenticated handshake

## 4. Firmware Maintenance Bus Factor (High)

**Risk:** The system becomes dependent on a single volunteer. When that person is unavailable, the fleet terminals cannot be updated or repaired.

**Mitigation:**
- Explicit ownership and succession discussion required **before** Phase 1 hardware purchase (see Open Questions)
- Firmware update path designed from the start (marina Wi-Fi or local USB)
- Documentation and simple build process so more than one person can maintain it
- Prefer simple, well-understood stacks over clever but fragile solutions

## 5. Token Expiry During a Sail (Medium)

**Risk:** Instrument data disappears or booking state causes confusion when a reservation window ends while the boat is still under way.

**Mitigation:**
- Hard rule: live instrument display is completely independent of pairing token state
- Token expiry only affects booking confirmation and log-upload privileges
- Documented and tested as a first-class edge case

## 6. Inconsistent NMEA Availability Across the Fleet (Medium)

**Risk:** Some boats may have only NMEA 0183, fragmented networks, or no usable instrument data at all. A one-size-fits-all installation plan will fail.

**Mitigation:**
- Survey each candidate pilot boat before hardware is ordered
- Accept that some boats may remain display-only or use a reduced feature set
- Document the actual data sources available on each vessel

## 7. Liability & Insurance (Medium)

**Risk:** Club liability if a modified instrument network contributes to an incident.

**Mitigation:**
- Board visibility required before permanent installations
- Prefer isolated, standards-compliant interfaces
- Keep changes minimal and reversible where practical

## Summary

The single-boat pilot approach remains the correct way to proceed. The items above must be addressed in design and process **before** the first permanent installation, not discovered afterwards.

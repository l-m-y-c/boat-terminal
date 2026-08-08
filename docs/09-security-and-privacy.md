# 09 – Security & Privacy

## Design Goals

- Pairing is limited to the member who currently holds a valid booking for that boat
- The terminal never becomes a long-term store of personal data
- Instrument data stays local to the boat unless the member explicitly shares it
- The club backend remains the source of truth for membership and bookings
- The system must not create new physical-security risks (e.g. advertising empty boats)

## Pairing Security Model

### Token
- Issued only by the club backend
- Bound to a specific boat and booking window
- Short-lived
- Verified by the terminal before any privileged state is accepted

### Terminal Identity
- Each physical terminal is provisioned with a unique device identity at install time
- The phone (or backend) verifies it is communicating with the expected terminal for that boat
- This prevents a malicious device in the marina from impersonating a club boat and harvesting pairing attempts

### Transport
- BLE with **LE Secure Connections**
- Prefer Out-of-Band (OOB) data supplied by the QR code shown on the terminal
- “Just Works” pairing is not acceptable for token exchange

### Lifecycle
- On check-out or token expiry the terminal securely forgets the pairing
- Instrument display continues regardless of pairing state (see below)

## Instrument Display Independence

Live NMEA data display is completely independent of pairing state.

- Token expiry mid-sail does **not** blank or degrade instrument screens
- Loss of phone, loss of cellular, or backend unavailability does not affect depth, wind, speed, etc.
- Pairing only gates booking confirmation, member display name, and log upload capability

This is a hard safety and usability requirement.

## BLE Advertising & Occupancy Privacy

- Advertising payloads must be minimal
- Booking status, member name, or “occupied / free” state must not be readable by passive scanners in the marina
- Detailed state is disclosed only after a successful authenticated handshake

This reduces the risk that the system could help a thief identify unoccupied boats.

## Data Stored on the Terminal

**Allowed**
- Current pairing state (display name, expiry) — volatile
- Recent instrument values — volatile
- Short local queue of hours or issue notes awaiting upload
- Device configuration (NMEA settings, brightness, device identity)

**Not stored long-term**
- Member passwords or account credentials
- Full booking history
- Precise track logs
- Photos

## Privacy Considerations

- The system does not continuously track boat location for the club
- Position data shown on the terminal stays local unless the member chooses to share it
- Issue reports and hour logs are associated with the booking and flow into existing club processes

## Physical Security

- Terminal lives in the cabin; normal boat security practices apply
- No high-value components beyond ordinary small electronics
- Firmware can use secure boot and flash encryption if the club desires

## Failure Modes

| Situation | Expected Behaviour |
|-----------|--------------------|
| Phone lost or dead | Instruments continue; manual check-out via existing web process remains possible |
| Backend unreachable | Previously issued token still works until its natural expiry; new pairings cannot be started |
| Token expires mid-sail | Booking status may clear; **instrument display is unaffected** |
| Terminal power loss | Returns to unpaired state on reboot; no sensitive data retained |
| Spoofed BLE device in marina | Phone rejects it because terminal identity cannot be verified |
| NMEA interface fault | Terminal drops off the bus cleanly; other instruments continue (see NMEA safety requirements) |

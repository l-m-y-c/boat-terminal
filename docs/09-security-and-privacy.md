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

The TF card is this boat's maintenance book. That rule was agreed on 2026-09-30 and is specified in [18 – Maintenance handoff](18-maintenance-handoff-and-auth.md). It replaces the earlier "short queue, no photos" note for this one kind of data.

**On the TF card (this sailboat only)**
- Maintenance items: title, description, category, priority, status, dates, reporter display name, leader notes
- Photos of those problems, as JPEGs
- A sync cursor and the list of items still waiting to upload

**Volatile, or configuration only**
- Current pairing state (display name, expiry)
- Recent instrument values
- Device configuration (NMEA settings, brightness, device identity, boat id)

**Not stored**
- Member passwords, OAuth access tokens, or refresh tokens
- Other people's bookings
- Other boats' maintenance books
- Precise track logs

The book is readable on the Waveshare with no phone. A new report is attributed to a member only while a boarding pass is paired. The phone carries pending items to lmyc.ca. The terminal does not hold a member session across crews.

## Loopback forward on the phone

While the relay runs, `127.0.0.1:10180` on the phone is forwarded to the terminal's web server on port 80, and `127.0.0.1:10110` carries the NMEA stream. Loopback is shared by every app on the phone, so any app there can reach those two ports without the LMYC login, exactly as any device on the boat hotspot already can. Nothing on port 80 may therefore need more trust than "on the boat Wi-Fi": no member data without its own check, no settings changes without pairing. The forward accepts at most four connections at once, drops idle ones after 30 s, and stops with the relay.

## Privacy Considerations

- The system does not continuously track boat location for the club
- Position data shown on the terminal stays local unless the member chooses to share it
- Issue reports are this boat's operational record. They stay on the card and flow to the club's maintenance list when a paired phone has a signal
- BLE advertising still must not reveal occupancy or a member name. See above.

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
| Terminal power loss | Returns to unpaired state on reboot. Member tokens were never on the device. This boat's maintenance book remains on the TF card |
| Spoofed BLE device in marina | Phone rejects it because terminal identity cannot be verified |
| NMEA interface fault | Terminal drops off the bus cleanly; other instruments continue (see NMEA safety requirements) |

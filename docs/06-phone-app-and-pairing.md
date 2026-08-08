# 06 – Phone App & Pairing

## Role of the Phone

The phone is the **member’s personal interface** to the club and the temporary bridge between the member and the boat terminal.

### Responsibilities

- Authenticate the member against the LMYC booking system
- Show current bookings and allow check-in
- Perform the secure pairing ceremony with the boat terminal
- Upload check-in, check-out, engine hours, and issue reports
- Provide cellular backhaul when needed
- Optionally display charts, weather, manuals, and richer information

### What the Phone Does *Not* Have to Do

- Be the primary instrument display while sailing (the terminal handles that)
- Stay connected to the terminal the entire time
- Have perfect cellular coverage while under way

## Pairing Flow (Proposed)

1. Member arrives at the boat and opens the LMYC app
2. App confirms the active booking for that boat
3. App requests a short-lived pairing token from the club backend
4. Member initiates pairing (QR code on terminal, or “Pair” button + BLE discovery)
5. Terminal and phone exchange the token over BLE (or local Wi-Fi)
6. Terminal shows “Paired with [Member Name] – Booking active until …”
7. While paired, the phone can push log entries and the terminal can request uploads
8. On check-out (or booking expiry), the pairing is revoked and the terminal returns to the public/available state

## Pairing Technologies

| Method | Pros | Cons | Recommendation |
|--------|------|------|----------------|
| Bluetooth LE | Low power, works without Wi-Fi, good security model | Slightly more complex to implement | **Primary** |
| Local Wi-Fi (terminal as AP) | Simple for data transfer | More power, potential interference | Secondary / fallback |
| QR code only | Very simple | Less secure if not combined with backend token | Useful as bootstrap |

## App Feature Set (v1)

**Must have**
- Login / membership status
- List of current and upcoming bookings
- Check-in and check-out actions
- Pairing with boat terminal
- Simple issue / condition report (text + optional photo)
- Engine hour entry

**Should have**
- View of live instrument data (mirrored from terminal while paired)
- Boat-specific notes and contacts
- Basic checklist (pre-departure / post-sail)

**Later**
- Offline chart support
- Integration with weather services
- Maintenance history view

## Offline Behaviour

- Pairing token can be cached for a short window
- Core instrument display on the terminal never depends on the phone
- Log entries can be queued on the phone and uploaded when connectivity returns

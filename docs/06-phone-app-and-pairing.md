# 06 – Phone App & Pairing

## Role of the Phone

The phone remains the member’s primary personal interface to LMYC. It already works well for viewing the calendar, managing bookings, and accessing boat information through the club’s web system.

The boat terminal simply adds a local, always-available companion on the vessel itself.

### Responsibilities of the Phone

- Authenticate the member against the existing LMYC booking system
- Show current bookings and support check-in when the member arrives at the boat
- Perform the secure pairing ceremony with the boat terminal
- Upload any observations, engine hours, or notes the member chooses to share
- Provide cellular connectivity when needed
- Continue to offer charts, weather, photos, and all the rich features members already use

### What the Phone Does *Not* Have to Do

- Be the primary instrument display while sailing (the terminal handles that)
- Stay permanently connected to the terminal
- Have perfect cellular coverage while under way

## Pairing Flow (Proposed)

1. Member has an active booking in the existing LMYC web system
2. Arrives at the boat and opens the LMYC companion (app or progressive web app)
3. Confirms the booking and taps “Pair with Boat”
4. The system issues a short-lived pairing token linked to that reservation
5. Member pairs with the terminal (QR code or Bluetooth)
6. Terminal shows the booking is active and begins displaying live instrument data
7. While paired, the member can optionally send notes or observations
8. At the end of the reservation the pairing is released and the terminal returns to its ready state

This flow is intended to feel like a natural extension of the booking the member already made online.

## Pairing Technologies

| Method | Pros | Cons | Recommendation |
|--------|------|------|----------------|
| Bluetooth LE | Low power, works without Wi-Fi | Slightly more complex | **Primary** |
| Local Wi-Fi (terminal as AP) | Simple data transfer | Higher power use | Secondary / fallback |
| QR code | Very simple bootstrap | Best combined with a backend token | Useful starting point |

## Suggested Feature Set (v1)

**Core**
- Login using existing LMYC credentials
- View and act on current bookings
- Pair / un-pair with the boat terminal
- Simple observation or condition note (text + optional photo)
- Engine hour entry

**Useful additions**
- Mirror of selected live instrument values while paired
- Boat-specific notes and contacts already maintained by the club
- Light pre-departure or post-sail checklist

**Later possibilities**
- Deeper integration with the existing maintenance items view
- Offline chart support
- Weather overlays

## Offline Behaviour

- The terminal’s instrument display never depends on the phone or cellular coverage
- A previously issued pairing token can remain valid for the booking window
- Any notes the member creates can be queued on the phone and uploaded when connectivity is available

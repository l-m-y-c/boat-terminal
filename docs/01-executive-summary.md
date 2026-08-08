# 01 – Executive Summary

## The Idea in One Paragraph

Every LMYC boat would have a small, always-on touchscreen terminal in the cabin.  
When a member arrives for a booked sail, they pair their phone with that terminal (primarily via QR code) for the duration of the reservation.  
The terminal acts as the boat’s digital logbook, maintenance book, and quick reference manual, while also showing live instrument data.  
The member’s phone continues to handle rich charts, weather, photos, and communication with the club’s existing web booking system.  
When the reservation ends, the pairing is released.

## Why This Approach?

LMYC already provides a solid online booking experience. This project extends that experience onto the boat in a lightweight, affordable way that solves real co-operative problems: clean handovers, consistent logging, visible maintenance history, and boat-specific knowledge that stays with the vessel.

| Approach | Strengths |
|----------|-----------|
| Commercial MFD on every boat | Polished navigation |
| Phone-only | Members already carry them |
| **Hybrid terminal + phone** | Low cost, open, always-available local companion focused on co-op needs |

## Key Benefits for LMYC

- Digital logbook and maintenance reporting that travels with the boat
- Clear digital handshake between the web booking and the physical vessel
- Boat-specific reference information available at the terminal
- Live instrument data visible in the cabin
- Low ongoing cost and fully open/maintainable by club volunteers

## Navigation Scope – Explicit Decision

We considered whether the terminal (especially with a larger display) should become a full navigation system capable of loading NOAA or Canadian raster charts and acting as a chartplotter.

**We deliberately chose not to pursue that path.**

Real navigation and autopilot control are best left to a professional chartplotter that is correctly paired with the autopilot. Members should continue to trust and use those systems for passage-making.

The Boat Terminal may still:

- Listen to (snoop on) NMEA data including waypoint and autopilot information
- Display basic navigation awareness (current waypoint, distance/bearing, simple track)
- Show limited map or weather context when useful

It will **not** attempt to replace a proper chartplotter or issue autopilot commands.

## What It Is Not

- Not a replacement for the existing booking system
- Not a full commercial chartplotter or navigation system
- Not an autopilot controller
- Not something that requires constant cellular coverage while sailing
- Not a system that stores sensitive personal data on the boat

## Rough Cost Reality

- Hardware per boat (terminal + isolated interfaces + enclosure): roughly CAD $155–300
- Development effort: primarily volunteer / club technical members
- Ongoing cost: essentially zero for basic operation

## Recommendation

This is a low-risk way to give every boat a useful digital companion that strengthens the co-operative’s shared responsibility model.  
A single-boat pilot would let the club evaluate the real value before any wider commitment.

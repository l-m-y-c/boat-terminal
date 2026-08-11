# 02 – Goals & Opportunities

## Building on What Already Works

LMYC already has a clear, web-based online booking system. Members log in, view the calendar, and reserve boats in a straightforward way. The fleet pages provide useful boat details, marina information, maintenance items, and certification requirements.

The boat terminal project is designed to **complement** this existing digital foundation rather than replace it. The aim is to extend the same seamless experience from the web browser onto the boat itself, while solving the everyday operational needs of a shared fleet.

## Core Design Principle

**The phone is the powerhouse and the primary user interface.**  
On-boat devices provide presence, persistence, glanceability, and local interaction that does not require the phone to stay unlocked or in hand.

## What the System Is For

The primary purpose is to serve the **co-operative**:

- Act as the shared digital **logbook** for each boat
- Act as the **maintenance book** (problem reporting + visibility of open and scheduled work)
- Act as a **boat-specific reference manual** that stays with the vessel
- Provide a clean digital handover when a member arrives for their booking
- Show live instrument data and useful status information without forcing constant phone use

## Opportunities to Enhance the On-Board Experience

1. **Clean digital handover**  
   Pairing via QR code confirms the booking, surfaces recent notes or open issues, and makes the transition between members visible and accountable.

2. **Consistent digital logbook**  
   Engine hours, fuel, crew, destination, and free-text notes can be captured easily and associated with the booking.

3. **Effortless maintenance reporting**  
   Problems noticed while on board can be recorded with a photo and short description and flow into the club’s existing tracking.

4. **Boat knowledge that stays with the boat**  
   Quick reference pages (systems overview, quirks, location of gear, starting procedures, etc.) reduce reliance on tribal knowledge.

5. **Live instruments + persistent status**  
   Depth, wind, speed, battery and other NMEA data are available at a glance. A large, low-power display can hold an overview (including map context) for long periods.

6. **Keep the system open and affordable**  
   Inexpensive, well-supported hardware and open software allow the whole fleet to be equipped without large capital outlay.

## Project Goals

### Primary Goals

- Serve as the boat’s digital logbook, maintenance book, and reference companion
- Create a smooth digital link between the existing booking system and the physical boat (QR pairing)
- Make it easy for members to report observations and log hours while on board
- Keep total cost per boat low enough that the club can equip the whole fleet
- Remain maintainable by more than one club volunteer
- Keep the member’s phone as the rich, primary interactive surface

### Secondary Goals

- Display live NMEA instrument data reliably on a fast interactive screen
- Provide a large, daylight-readable, ultra-low-power status / overview board
- Support simple checklists and boat-specific notes
- Allow basic navigation *awareness* and occasional high-quality map overviews (see Non-Goals)
- Work offline for core functions (cellular only needed for pairing and uploading logs)

### Non-Goals (Explicitly Out of Scope)

We considered building a full navigation system with larger displays capable of loading NOAA or Canadian raster charts and acting as a chartplotter / autopilot interface.

**We chose not to pursue that direction.**

Members are better served by continuing to trust a professional chartplotter correctly paired with the autopilot for real navigation and course control.

Therefore the following remain out of scope:

- Full interactive chartplotting or route planning on either on-boat display
- Acting as the primary navigation system
- Sending commands to the autopilot
- Replacing the official logbook or any legal requirements
- Real-time tracking of boats by the club while under way
- Attempting fluid “moving map” animation on the E-Ink panel

### Allowed Navigation-Related Features

While neither on-boat display will become a chartplotter, the system **may**:

- Listen to (snoop on) NMEA data including active waypoints, routes, and autopilot status
- Display basic navigation awareness on the interactive terminal
- Push high-quality static or slowly updating map / route overviews from the phone to the large E-Ink board (on demand or every 30–60 seconds). At typical club speeds (5–7 kn) and the ranges shown on such overviews, this update rate is entirely practical.

## Success Criteria for a Pilot

A successful pilot on one boat would demonstrate:

- Reliable digital logbook and maintenance reporting tied to bookings
- Smooth QR-based pairing that feels like a natural extension of the existing booking process
- Useful boat-specific reference information available at the terminal
- Live instrument data that remains available even if pairing expires
- A large, readable status / overview board that stays useful with infrequent updates
- Hardware that is practical and reliable in normal cabin (and optionally protected outdoor) conditions
- Clear evidence that the system helps the co-operative rather than adding complexity

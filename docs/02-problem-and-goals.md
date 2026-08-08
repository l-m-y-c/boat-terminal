# 02 – Goals & Opportunities

## Building on What Already Works

LMYC already has a clear, web-based online booking system. Members log in, view the calendar, and reserve boats in a straightforward way. The fleet pages provide useful boat details, marina information, maintenance items, and certification requirements.

The boat terminal project is designed to **complement** this existing digital foundation rather than replace it. The aim is to extend the same seamless experience from the web browser onto the boat itself, while solving the everyday operational needs of a shared fleet.

## What the System Is For

The primary purpose is to serve the **co-operative**:

- Act as the shared digital **logbook** for each boat
- Act as the **maintenance book** (problem reporting + visibility of open and scheduled work)
- Act as a **boat-specific reference manual** that stays with the vessel
- Provide a clean digital handover when a member arrives for their booking
- Show live instrument data in the cabin as a convenient secondary benefit

## Opportunities to Enhance the On-Board Experience

1. **Clean digital handover**  
   Pairing via QR code confirms the booking, surfaces recent notes or open issues, and makes the transition between members visible and accountable.

2. **Consistent digital logbook**  
   Engine hours, fuel, crew, destination, and free-text notes can be captured easily and associated with the booking.

3. **Effortless maintenance reporting**  
   Problems noticed while on board can be recorded with a photo and short description and flow into the club’s existing tracking.

4. **Boat knowledge that stays with the boat**  
   Quick reference pages (systems overview, quirks, location of gear, starting procedures, etc.) reduce reliance on tribal knowledge.

5. **Live instruments in the cabin**  
   Depth, wind, speed, battery and other NMEA data are available at a glance without needing to unlock a phone.

6. **Keep the system open and affordable**  
   Inexpensive, well-supported hardware and open software allow the whole fleet to be equipped without large capital outlay.

## Project Goals

### Primary Goals

- Serve as the boat’s digital logbook, maintenance book, and reference companion
- Create a smooth digital link between the existing booking system and the physical boat (QR pairing)
- Make it easy for members to report observations and log hours while on board
- Keep total cost per boat low enough that the club can equip the whole fleet
- Remain maintainable by more than one club volunteer

### Secondary Goals

- Display live NMEA instrument data reliably
- Support simple checklists and boat-specific notes
- Allow basic navigation *awareness* (see Non-Goals below)
- Work offline for core functions (cellular only needed for pairing and uploading logs)

### Non-Goals (Explicitly Out of Scope)

We considered building a full navigation system with larger displays capable of loading NOAA or Canadian raster charts and acting as a chartplotter / autopilot interface.

**We chose not to pursue that direction.**

Members are better served by continuing to trust a professional chartplotter correctly paired with the autopilot for real navigation and course control.

Therefore the following remain out of scope:

- Full chartplotting or route planning on the terminal
- Acting as the primary navigation system
- Sending commands to the autopilot
- Replacing the official logbook or any legal requirements
- Real-time tracking of boats by the club while under way
- High-brightness outdoor / helm displays (cabin focus first)

### Allowed Navigation-Related Features

While the terminal will not become a chartplotter, it **may**:

- Listen to (snoop on) NMEA data including active waypoints, routes, and autopilot status
- Display basic navigation awareness (current waypoint, distance, bearing, cross-track error, simple track history)
- Show limited map context or weather information when it adds value without pretending to be the primary navigation tool

## Success Criteria for a Pilot

A successful pilot on one boat would demonstrate:

- Reliable digital logbook and maintenance reporting tied to bookings
- Smooth QR-based pairing that feels like a natural extension of the existing booking process
- Useful boat-specific reference information available at the terminal
- Live instrument data that remains available even if pairing expires
- Hardware that is practical and reliable in normal cabin conditions
- Clear evidence that the system helps the co-operative rather than adding complexity

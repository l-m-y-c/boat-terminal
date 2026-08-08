# 08 – User Flows / Journeys

These journeys focus on the co-operative value of the system: clean handovers, logging, maintenance visibility, and boat knowledge. Instrument display and limited navigation awareness are supporting features.

## 1. Member Arrives for a Booked Sail

1. Member already has a reservation in the LMYC online calendar.
2. Arrives at the boat and opens the LMYC companion on their phone.
3. Scans the QR code displayed on the cabin terminal.
4. Phone and terminal complete secure pairing (booking token + terminal identity).
5. Terminal confirms the booking is active and shows:
   - Member / booking window
   - Any open maintenance items or notes left by the previous user
   - Quick access to the boat’s reference pages
6. Live instruments are already available (they do not depend on pairing).
7. Member can put the phone away or continue using it for charts and weather as usual.

## 2. While Under Way

- Terminal continues to show live depth, wind, speed, battery, and other available data.
- Basic navigation awareness (active waypoint, distance/bearing, simple track) may be visible if the data is present on the NMEA bus.
- The professional chartplotter and autopilot remain the primary navigation tools.
- Phone remains available for full charts, weather, and communication.
- No constant connection between phone and terminal is required.

## 3. Member Notices Something Worth Reporting

1. Opens the companion on their phone (or uses a simple form on the terminal).
2. Adds a short note and optional photo.
3. The information is automatically associated with the current boat and booking.
4. It flows into the club’s existing maintenance tracking when connectivity is available.

## 4. End of the Reservation – Digital Logbook Entry

1. Member returns to the marina.
2. Opens the companion (or uses the terminal) and completes a short log entry:
   - Engine hours
   - Fuel note (if relevant)
   - Crew / guests
   - Destination or area sailed
   - Free-text observations
3. Optionally records final closing notes or checklist items.
4. Pairing is released (or simply expires with the booking window).
5. Terminal returns to its ready state for the next member.

## 5. Maintenance or Training Use

- Volunteers or instructors can view live instruments and open maintenance items at any time.
- Elevated or longer-lived access can be added later if the club finds it useful.
- The system is designed to stay out of the way when it is not needed.

## 6. Phone Unavailable or Dead

- The terminal continues to show live instrument data and any locally cached boat reference information.
- Booking and check-out can continue through the existing web process.
- Pairing is a convenience that enhances the experience; it is not a requirement to use the boat.

## 7. Token Expires Mid-Sail (Edge Case)

- Booking status on the terminal may clear.
- **Live instrument display and any basic navigation awareness continue without interruption.**
- This is a hard requirement so that a long sail never loses depth or wind data simply because a booking window ended.

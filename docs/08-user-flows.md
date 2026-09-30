# 08 – User Flows / Journeys

These journeys reflect the dual-display architecture:

- **Phone** = primary interactive surface and powerhouse
- **7″ colour touch** = fast cabin terminal (pairing, logbook, live instruments, quick reference)
- **13.3″ Spectra 6 E-Ink** = large, daylight-readable, low-power status and overview board

The professional chartplotter remains the primary navigation tool. The E-Ink is deliberately used for high-quality *static or slowly updating* map and route overviews rather than fluid interaction.

---

## 1. Member Arrives for a Booked Sail

1. Member already has a reservation in the LMYC online calendar.
2. Arrives at the boat and opens the LMYC companion on their phone.
3. Scans the QR code on the **7″ terminal**.
4. Phone and terminal complete secure pairing (booking token + terminal identity).
5. **7″ terminal** confirms the booking is active and shows:
   - Member name / booking window
   - Open maintenance items or notes left by the previous user
   - Quick access to boat-specific reference pages
6. **13.3″ E-Ink** updates (or is pushed) to a clear status board:
   - Current booking / who is checked in
   - Open issues summary
   - Optional high-level instrument or next-leg overview
7. Live instruments are already available on the 7″ terminal (they do not depend on pairing).
8. Member can put the phone away or continue using it for full charts and weather.

---

## 2. While Under Way – Instruments & Status

- **7″ terminal** continues to show live depth, wind, speed, battery, and other NMEA data.
- Basic navigation awareness (active waypoint, distance/bearing, cross-track) may appear on the 7″ if the data is on the bus.
- **13.3″ E-Ink** holds a stable, high-resolution overview that is readable at a glance from the cabin or (if mounted outside) from the cockpit.
- The professional chartplotter and autopilot remain the primary navigation tools.
- Phone remains available for full interactive charts, weather, and communication.
- No constant connection between phone and either display is required for core instrument function.

---

## 3. Navigation Overview on the Large E-Ink

This is an intentional, high-value use of the 13.3″ display.

1. On the phone the member (or the companion app) prepares a clean map / route overview:
   - Current position
   - Active route or next waypoints
   - Relevant chart context at a useful range
2. The image is pushed to the E-Ink (on demand or on a 30–60 second cadence).
3. The E-Ink performs a full refresh and then holds the image with near-zero power.
4. At typical club speeds (5–7 kn) and the ranges shown on such overviews, a 30–60 s update rate keeps the picture useful without fighting the panel’s physics.
5. The member can glance at a large, daylight-readable chart board without keeping the phone unlocked or in a specific place.

This is **not** a live moving-map chartplotter. It is a persistent, high-quality navigation *overview* that complements the phone and the professional plotter.

---

## 4. Member Notices Something Worth Reporting

1. Opens the companion on their phone, or uses the form on the 7″ terminal.
2. Adds a short note. Photos are taken on the phone, converted to JPEG, and stored on the boat's card with the note.
3. The boat is the terminal they are paired to. Category and priority match the lmyc.ca form.
4. The note is on the card immediately, including with no cell service.
5. The phone posts it to the club's maintenance list when it next has a signal, and stores the website id on the card so a retry does not file it twice.
6. The next person can read it on the 7″, or on their phone over the terminal's Wi-Fi, with no coverage. See [18](18-maintenance-handoff-and-auth.md).

---

## 5. End of the Reservation – Digital Logbook Entry

1. Member returns to the marina.
2. Opens the companion (or uses the 7″ terminal) and completes a short log entry:
   - Engine hours
   - Fuel note (if relevant)
   - Crew / guests
   - Destination or area sailed
   - Free-text observations
3. Optionally records final closing notes or checklist items.
4. Pairing is released (or simply expires with the booking window).
5. **7″ terminal** returns to its ready / QR state.
6. **13.3″ E-Ink** returns to a neutral “boat available / last log summary / open issues” board for the next member.

---

## 6. Maintenance or Training Use

- Volunteers or instructors can view live instruments on the 7″ and the persistent status board on the E-Ink at any time.
- Elevated or longer-lived access can be added later if the club finds it useful.
- The system is designed to stay out of the way when it is not needed.

---

## 7. Phone Unavailable, or No Cell Coverage

- The **7″ terminal** continues to show live instrument data.
- This boat's maintenance book, including photos, stays readable on the 7″. It does not need the phone or a signal.
- A phone that already fetched a boarding pass can still pair and add a report. The report waits on the card until some phone can reach lmyc.ca.
- The **13.3″ E-Ink** is a separate display. It holds the last picture the phone sent it.
- Booking and check-out can continue through the website when a signal exists.
- Using the boat does not require pairing. Pairing attributes new notes to the member.

---

## 8. Token Expires Mid-Sail (Edge Case)

- Booking status on the displays may clear.
- **Live instrument display on the 7″ continues without interruption.**
- The E-Ink simply holds whatever image was last written.
- This is a hard requirement so that a long sail never loses depth or wind data simply because a booking window ended.

---

## 9. Outdoor / Cockpit Glance (Optional Mounting)

If the 13.3″ E-Ink is mounted in a waterproof enclosure with a clear view from the cockpit and powered by a large lithium pack:

- It becomes a large, sunlight-readable “chart board” and status panel that can be seen without going below.
- Updates still follow the same on-demand or 30–60 s model.
- The 7″ remains the interactive cabin device.
- The phone remains the rich navigation and data-entry surface.

This configuration plays to the E-Ink’s strengths (daylight readability + extreme low static power) while keeping interaction where it belongs.

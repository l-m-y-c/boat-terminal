# 08 – User Flows

## 1. Member Checks Out a Boat (Happy Path)

1. Member has an active booking in the LMYC system
2. Arrives at the marina and opens the LMYC phone app
3. Selects the booking → taps “Check In & Pair”
4. App contacts the club backend and receives a short-lived pairing token
5. Member approaches the boat terminal and initiates pairing (QR or BLE)
6. Terminal displays “Paired with [Name] – Valid until [time]”
7. Terminal begins showing live instrument data
8. Member can now sail; phone can be put away or used for charts/weather

## 2. While Under Way

- Terminal continues to show depth, wind, speed, battery, etc.
- Phone may be used for navigation, weather, or communication
- If the member wants, the phone can mirror selected instrument data while still paired
- No constant cellular connection is required for the terminal to function

## 3. Member Notices a Problem

1. Opens the phone app (or uses a simple form on the terminal)
2. Creates an issue report (text + optional photo)
3. Report is queued and uploaded when cellular is available
4. Club maintenance team sees the report linked to the boat and booking

## 4. End of Sail – Check Out

1. Member returns to the marina
2. Opens the app and taps “Check Out”
3. Optionally enters final engine hours and any closing notes
4. App notifies the backend; pairing token is revoked
5. Terminal returns to the “Available” or public status screen
6. Next member can pair when their booking becomes active

## 5. Maintenance Volunteer

- Can pair with elevated privileges (if implemented) or simply use the terminal in read-only mode
- Sees recent issue reports and hour logs for that boat
- Can clear or acknowledge items as they are resolved

## 6. No Phone / Phone Dead

- Terminal still shows live instruments (core value preserved)
- Check-in/out can fall back to the existing manual process
- Pairing is a convenience, not a hard requirement for basic use of the boat

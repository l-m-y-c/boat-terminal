# 06 – Phone App & Pairing

## Role of the Phone

The phone remains the member’s primary personal interface to LMYC. It already works well for viewing the calendar, managing bookings, and accessing boat information through the club’s web system.

The boat terminal adds a local, always-available companion on the vessel itself.

### Responsibilities of the Phone

- Authenticate the member against lmyc.ca (OAuth 2.0 authorization code + PKCE, same WordPress user as the website). See [18](18-maintenance-handoff-and-auth.md)
- Show current bookings and support check-in when the member arrives at the boat
- Perform the secure pairing ceremony with the boat terminal
- Upload any observations, engine hours, or notes the member chooses to share
- Provide cellular connectivity when needed
- Continue to offer charts, weather, photos, and all the rich features members already use

### What the Phone Does *Not* Have to Do

- Be the primary instrument display while sailing
- Stay permanently connected to the terminal
- Have perfect cellular coverage while under way

## Pairing Flow (Proposed)

1. Member has an active booking in the existing LMYC web system
2. Arrives at the boat and opens the LMYC companion (app or progressive web app)
3. Confirms the booking and initiates pairing
4. The backend issues a short-lived, boat-specific pairing token
5. Member completes pairing with the terminal (QR + BLE recommended)
6. Terminal and phone mutually authenticate
7. Terminal shows the booking is active and continues displaying live instrument data
8. At the end of the reservation (or on explicit check-out) the pairing is released

This flow is intended to feel like a natural extension of the booking the member already made online.

## Security Requirements for Pairing

“Standard BLE encryption” is not sufficient. The following are required:

### 1. Mutual Authentication
- The **token** proves the member has a valid booking for this boat.
- The **terminal** must also prove its identity so a member cannot be tricked by a spoofed device advertising as “Boat X” in the marina.

### 2. Recommended BLE Approach
- Use **LE Secure Connections**
- Prefer Out-of-Band (OOB) pairing where the QR code displayed on the terminal (or a short numeric code) supplies the OOB data
- Avoid “Just Works” pairing for any token exchange that matters

### 3. Terminal Identity
- Each terminal is provisioned at build / install time with a unique device key or certificate
- The phone (or backend) can verify that it is talking to the expected physical terminal for that boat

### 4. Token Properties
- Issued only by the club backend
- Bound to a specific boat and booking window
- Short-lived (booking duration + modest grace period)
- Single-use or tightly limited replay window
- Securely forgotten by the terminal on expiry or check-out

## Instrument Display Independence (Critical Edge Case)

**The live instrument display on the terminal must never depend on a valid pairing token.**

- Token expiry, check-out, or loss of the phone must not blank or disable depth, wind, speed, or other instrument data.
- Pairing only controls booking status, member name display, and the ability to push logs from the terminal.
- This is a hard requirement, not a preference. A sailor mid-passage must not lose instruments because a booking window ended.

## Advertising & Discovery Behaviour

- The terminal should advertise minimally.
- Detailed booking or occupancy state must not be broadcast in the clear.
- Full state is revealed only after a successful handshake.
- This prevents passive observers in the marina from easily determining which boats are currently unoccupied.

### Bench implementation (current firmware / app)

The 7″ terminal advertises as `LMYC-<boat>-XXXX`: the boat ID (up to 16 characters) and the last two BT MAC bytes, 26 characters at most, for example `LMYC-BENCH-01-3F2A`. The boat ID lets a phone tell neighbours apart; the MAC tail keeps two bench boards with the same ID distinct. Diagnostics shows the same string on its **BLE name** row. BLE advertising is 31 bytes: a 128-bit service UUID plus the local name **cannot** both fit in the primary packet. The firmware therefore puts **flags + complete local name in the primary ADV** and the LMYC 128-bit UUID only in the scan response.

**Do not use Android Settings → Bluetooth to find the terminal.** System Settings hide most LE-only peripherals. Use the LMYC app.

### The journey

The app and the terminal show the same four stops, in the same order, with the same names: **Find**, **Connect**, **Wi-Fi**, **NMEA**. A finished stop is ticked, the current one is ringed, and a stop that needs the member turns red. Under the bar there is one title, one sentence, and at most one main button. Diagnostics are not on the journey: on the phone they are the fourth on-board tab, and a small **Diagnostics** link on each failed step (**Could not connect**, **No boat found**, a Wi-Fi problem); on the terminal they are on its Diagnostics page. The settings icon on the app bar opens **Settings**.

1. **Find.** The main button is **Scan QR code**, because a marina has many boats and the code names one. **Find nearby boats** is the second choice: it listens for about four seconds and lists every `LMYC-*` terminal, strongest first, with the signal in words. One terminal, or the boat this phone joined last, starts a five-second **Joining in N s** bar; **Not this boat** stops it and **Join now** skips it. Settings can change the countdown to 3 or 10 s, or turn automatic joining off so the list always shows. **Choose another boat** also offers **Paste a pairing link** for an `lmyc://` link sent by message. After a join the app remembers the terminal and the next launch joins it the same way, with no tap. The terminal shows the QR code at this stop.
2. **Connect.** Bluetooth connect and the OOB check. The join reads the live `lmyc://` link, OOB included, from payload characteristic `…0010`, so a nearby join needs no QR. Anyone in Bluetooth range can pair and read the hotspot passphrase; that is a deliberate choice for now. A wrong code is red on both screens, and the terminal shows the QR again.
3. **Wi-Fi.** After the OOB check the phone reads `…000040`: the hotspot name, passphrase, and `192.168.4.1`. The read is `WAIT` until the OOB matches. The app accepts only a name that starts with `LMYC-`. Its relay service then joins that network **for the LMYC app alone** (`WifiNetworkSpecifier`): nothing is saved on the phone, so there is no Save prompt, no Wi-Fi list and no "Always connect". The first time, Android asks once, **Connect to LMYC-<boat>?**; it remembers the answer and later joins without asking (the log reads *Approved network found. Allowing user dialogue to get bypassed*). A refused dialog is a red step with **Try again**. The phone has one Wi-Fi connection, so while the app holds the boat link the phone's own Wi-Fi network (home, marina) is off and its internet carries on over mobile data; **Leave the boat** releases the link and Android rejoins its saved networks. Wi-Fi settings show the boat network as connected while that lasts. The app also removes the network suggestion an older version registered.
4. **NMEA.** The app starts its relay (below) and waits until the terminal's output reaches it. The terminal sends the standard heartbeat `$IIHBT,15.0,A,<n>` to each new TCP client at once and to everyone every 15 s, so this stop completes on a quiet NMEA 2000 bus too. The phone treats 20 s with nothing, not even a heartbeat, as the terminal gone. A quiet bus is not a fault: the heartbeat shows the link works, so both screens say **Waiting for boat data** in neutral grey for as long as the instruments are silent. Red is kept for a real bus fault (bus-off or a failed controller).

**Arrived** shows **On board <boat>** with a big tick for about two seconds (tap to skip), then the on-board screens (Settings can skip this screen and pick which tab opens). The four ticked stops stay as a slim strip under the title, and a bottom bar switches between **Boat**, **Instruments**, **NMEA data** and **Diagnostics**; swiping sideways moves between them and wraps round from the last to the first. While the terminal simulates, the boat name in the title carries an amber **Simulation** badge on every screen; tapping it explains that position, depth and wind are not real and how to switch the terminal to NMEA 2000. **Boat** has the data source pill (**Live · NMEA 2000**, **Simulation**, **Waiting for boat data**, **Bus fault**, tap for a sentence on it), **Connect a chart app** and **Leave the boat**. **Instruments** mirrors the terminal's Instruments screen: depth (m or ft), speed (kn or m/s SOG), course (° COG) and wind (kn or m/s AWS with AWA), in the units chosen in Settings, read from the phone's relay on 127.0.0.1:10110 and decoded like `nmea_state.cpp`; a value with nothing new for 5 s shows —. **NMEA data** has the counters from BLE `…000050` (traffic, lost frames, buffer peak, chart-app delivery); every panel opens a sheet explaining exactly what it counts. **Diagnostics** opens with one verdict (for example **All links good**, **Phone is not on LMYC-<boat>**, **Chart apps cannot connect**), then a card per link: Bluetooth (terminal, ID, signal, pairing check), boat Wi-Fi (network, terminal address), the phone's Wi-Fi, the relay on the phone (service, port, link to the terminal, last data, chart apps connected), and the boat's data (BLE counters, plus the terminal's `/status.json` `tcp`, `nmea_good` and `nmea_bad` read every 5 s through `127.0.0.1:10180`). Each card and the verdict open a help sheet. Actions: **Reconnect Bluetooth**, **Sentences**, **Copy report** and **Share report** (plain text: every card plus the recent events, no passphrase), and **Forget this boat**. **Recent events** lists the last 200 connection events kept in memory; they are lost when the app closes. Arrival depends on chart data, not on Bluetooth: if Bluetooth drops afterwards the Boat screen says chart apps keep working and offers **Reconnect**.

**Settings** are kept on the phone and apply at once; none is sent to the terminal: the saved boat (with **Forget**), automatic joining and its countdown, the arrival screen, the tab to open on, speed and wind in knots or m/s, depth in metres or feet, keeping the screen on while Instruments shows, light, dark or system theme, and About (app version, terminal ID, a link to this page).

The app checks the phone's networks every two seconds while it is on screen, and at once when it comes back from Wi-Fi settings, so every stop moves on by itself.

The terminal works its stops out from what it can see: a Bluetooth link (**Connect**), a paired phone that read the hotspot details this boot (**Wi-Fi**), a station on the hotspot (**NMEA**), and a station plus a TCP client on port 10110 (**On board**). The app's relay is that TCP client. The **QR** chip on the top bar brings the code back for a second phone.

### Why chart apps use the phone itself

Android sends each app's traffic over one default network. The boat hotspot has no internet, so with mobile data on, Android keeps mobile data as the default, and an ordinary connection to `192.168.4.1` goes out over the cell network and fails. Apps cannot add a route for `192.168.4.0/24`. Measured on a Galaxy S948W with Android 17:

- On the boat Wi-Fi with mobile data on, TCP to `192.168.4.1:10110` timed out, while a socket the app bound to the boat network connected.
- Android's **No internet, Always connect?** dialog makes the boat network the default, but it was blocked when the LMYC app was in front, Android did not keep the answer for a network suggestion, and an unaccepted network was dropped about a minute later. Turning mobile data off works but takes the phone's internet away. Neither is acceptable for members.

Saving the network for the whole phone was tried first and dropped for those reasons. The app runs a foreground service, **Connected to the boat**, while sailing. It joins the boat Wi-Fi for itself, reads TCP `192.168.4.1:10110` over that network, and repeats the stream on loopback: TCP on `127.0.0.1:10110`, and each sentence as one UDP datagram to `127.0.0.1` ports 10110 and 2000. Loopback is shared by every app on the phone and no routing decision touches it. Chart apps are set once:

| App | Setting |
|-----|---------|
| Navionics | A connected device: **UDP**, address `127.0.0.1`, port **10110** |
| i-Boating | NMEA over **UDP**, port **10110** (it asks no address) |

The terminal's own UDP broadcasts arrive on a network that belongs to the LMYC app, so other apps never see them; the loopback datagrams are what chart apps receive. Two chart apps cannot listen on the same UDP port, so the relay also sends to port 2000 for a second one, though Android pauses the app that is not on screen anyway. Everything depends on the relay running: Leave the boat, or stopping it from its notification, ends the feed.

**Which address to use.** Chart apps on the phone running the LMYC app read `127.0.0.1`, port 10110 (TCP or UDP), because only the LMYC app is on the boat Wi-Fi. Every other device, such as a second phone, a tablet or a laptop running Navionics, joins the terminal's hotspot as before and reads `192.168.4.1:10110` over TCP, or the UDP broadcasts on 10110.

**Android 10 or newer.** The app joins the boat Wi-Fi with `WifiNetworkSpecifier`, which Android 9 and older do not have, so the app's minimum is Android 10 (API 29). Older phones can still join the hotspot by hand and use `192.168.4.1:10110` like any other device.

**Recovery.** Android asks **Connect** only while the app is open. If the member says no, the setup screen says so and **Try again** asks again. The relay keeps the Wi-Fi details in memory only. If Android restarts it in the background, it asks nothing; its notification reads **Tap to reconnect to the boat**, and opening the app hands the details back. A changed boat or passphrase replaces the old request. The app counts the terminal as gone after 35 s with no data (two missed 15 s heartbeats); the relay's read timeout is 45 s.

The app's own HTTP to the terminal (`/status.json`, and the maintenance book in the full build) would also go out over mobile data, so the relay forwards `127.0.0.1:10180` to the terminal's port 80 over the boat link. The book uses that address; club sync to lmyc.ca keeps the phone's internet. The Sentences console reads TCP through `127.0.0.1:10110` the same way.

## Suggested Feature Set (v1)

**Core**
- Login using existing LMYC credentials
- View and act on current bookings
- Secure pair / un-pair with the boat terminal
- Simple observation or condition note (text + optional photo)
- Engine hour entry

**Useful additions**
- Mirror of selected live instrument values while paired
- Boat-specific notes already maintained by the club
- Light pre-departure or post-sail checklist

**Later possibilities**
- Bookings through the same member token
- Offline chart support
- Weather overlays

## Maintenance book on the boat

The phone is the courier for [the on-card maintenance book](18-maintenance-handoff-and-auth.md).

- While the phone still has coverage it fetches a short-lived boarding pass for this member and this boat. Pairing in a bay with no signal uses that pass. It does not call home again.
- Photos and item text move between the phone and the terminal over the SoftAP, not over BLE.
- The phone posts pending items to lmyc.ca with the member's bearer token, and writes club-side edits back onto the card.
- The terminal never receives the member's refresh token.

## Offline Behaviour

- Instrument display never depends on the phone or cellular coverage
- A previously issued pairing token remains valid until its natural expiry
- Notes created on the phone can be queued and uploaded when connectivity returns

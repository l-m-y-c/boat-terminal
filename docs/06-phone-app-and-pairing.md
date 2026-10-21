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

**Do not use Android Settings → Bluetooth to find the terminal.** System Settings hide most LE-only peripherals. The app's landing page is **Discover**. It listens for about four seconds and lists every `LMYC-*` terminal it hears, strongest signal first, by boat name, with the signal in words and the full BLE name. One terminal, or the boat this phone joined last, starts a five-second **Joining in N s** bar. Tap the bar or **Cancel** to stop and see the list; **Join now** skips the wait. Several terminals and no known boat go straight to the list. After a join the app remembers that terminal, so the next launch looks for it and joins with the same countdown, with no tap.

The join reads the live `lmyc://` link, OOB included, from payload characteristic `…0010`, so Discover needs no QR. The terminal itself is not protected this way: anyone in Bluetooth range can pair and read the hotspot passphrase. That is a deliberate choice for now. What the app does protect is the phone: it saves only a Wi-Fi name that starts with `LMYC-`.

**Scan QR code** (in the app, or with the phone camera app) is the backup when the list is ambiguous or a terminal does not show up. **Find terminals** stays under Diagnostics.

The home screen puts the club logo on the left half and the pairing QR on the right. The QR stays up while the terminal is advertising. Once the phone is paired that code parks, because it is the secret for this boot. A **QR** button on the top-left status row brings the same code back. The other two buttons on that row are icons: the position source (phone, simulator, or the boat) and the link. Tap one to read the full line (`Advertising`, `Connected`, `Paired`, and the failures). Pairing does not leave that screen. The screen buttons sit on a rail behind a tab on the right edge. **Reset BLE** is the top button on the Diagnostics rail and restarts advertising if a previous connect left the radio stuck.

After that OOB check the phone reads characteristic `…000040`. The terminal answers with the SoftAP name, passphrase, and `192.168.4.1`. The read stays `WAIT` until the OOB matches, and the passphrase is not in the QR or the advertisement. Join saves that network for the whole phone and Android shows its own prompt. This is the same scan, with no extra tap. The app then tries port 10110 for about 12 seconds while Android moves the phone. Android will not drop a Wi-Fi network that has internet in favor of the boat hotspot, which has none. So aboard, with no other Wi-Fi around, the phone joins by itself. On the bench next to home Wi-Fi it stays put. The home screen then shows **Open Wi-Fi settings**, and the app checks again when the user comes back. **Join boat Wi-Fi** shows only when the prompt was closed without saving or the terminal did not share the network. Until the phone itself is joined to `LMYC-<boat>` in Wi-Fi settings, Navionics and i-Boating cannot see `192.168.4.1`. Once it is, TCP port 10110 and UDP broadcasts to `192.168.4.255` on ports 10110 and 2000 are the NMEA stream. The UDP socket is bound to the hotspot address so those broadcasts are not sent out the terminal's upstream Wi-Fi.

After the phone is paired, the home screen leads with a **Boat data** card: a pill that reads **Live · NMEA 2000** (green), **Simulation** (amber), or **No bus data** / **Bus fault** (red), one line in plain words, and on a live bus whether any frame has been lost. The app reads it from BLE `…000050` every two seconds. Tap the card for the full panel: traffic (messages and frames a second, peak, PGN types, devices), integrity (lost frames, bus errors, the receive buffer's high-water mark against its 2048 frames), and delivery to chart apps (apps connected, sentences sent, not delivered, the per-app buffer peak). On the simulator it says plainly that this is not the boat's data, and how to switch on the terminal. The phone shows the source; the switch is on the terminal.

Below the card are **Chart apps** (Wi-Fi name, address, TCP 10110, UDP 10110 and 2000) and **Maintenance** (a defect report, or a log entry that is not built yet). The tune icon on the app bar opens **Diagnostics**: the link facts, Sentences, the chart snapshot, and Find terminals.

The phone's Link card and the terminal's Link screen show the same four facts: Bluetooth paired, the hotspot name, whether the phone itself is on that hotspot, and whether TCP port 10110 on `192.168.4.1` opened. On the glass, **Phones on it** is the number of stations associated to the hotspot. The Wi-Fi icon on Instruments is amber at 0 and green above 0. **Uplink** is the terminal's own upstream network, not the phone. **NMEA listeners** counts TCP clients on port 10110. The same line is printed on the UART Type-C console when any of those counts change: `LINK <ssid> phones N  nmea N  ble <status>  uplink <ip|none>`. A green Bluetooth icon does not mean the hotspot has a phone on it.

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

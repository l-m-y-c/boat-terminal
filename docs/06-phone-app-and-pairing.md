# 06 – Phone App & Pairing

## Role of the Phone

The phone remains the member’s primary personal interface to LMYC. It already works well for viewing the calendar, managing bookings, and accessing boat information through the club’s web system.

The boat terminal adds a local, always-available companion on the vessel itself.

### Responsibilities of the Phone

- Authenticate the member against the existing LMYC booking system
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

The 7″ terminal advertises as `LMYC-XXXX` (last two BT MAC bytes). BLE advertising is 31 bytes: a 128-bit service UUID plus the local name **cannot** both fit in the primary packet. The firmware therefore puts **flags + complete local name in the primary ADV** and the LMYC 128-bit UUID only in the scan response.

**Do not use Android Settings → Bluetooth to find the terminal.** System Settings hide most LE-only peripherals. Open the LMYC app and tap **Find terminals**. Pairing still requires the on-screen QR (`lmyc://pair?...`) because the OOB secret rotates every boot.

Watch the terminal **DIAGNOSTICS** card: `Advertising` → `Connected` → `Paired`. **Reset BLE** restarts advertising if a previous connect left the radio stuck.

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
- Deeper integration with existing maintenance items
- Offline chart support
- Weather overlays

## Offline Behaviour

- Instrument display never depends on the phone or cellular coverage
- A previously issued pairing token remains valid until its natural expiry
- Notes created on the phone can be queued and uploaded when connectivity returns

# 05 – NMEA Connectivity

## Supported Standards

The terminal is designed to accept both major marine data standards:

### NMEA 0183
- Serial protocol (typically 4800 or 38400 baud)
- Common on older instruments and many depth / wind / GPS units
- Connected via isolated RS-422 / RS-485 or opto-isolated UART
- Relatively low risk to the rest of the boat’s systems

### NMEA 2000
- CAN-bus based, modern standard
- Preferred on newer boats
- Requires a careful connection: bus-powered with a single ground, or isolated (see safety section below)
- Mature open-source library ecosystem exists (`NMEA2000` + community ESP32 ports)

## What Data Should Be Displayed?

**Minimum useful set for cabin terminal:**

- Depth (below transducer / below keel)
- Boat speed (through water or SOG)
- Wind speed & direction (apparent / true if available)
- Heading or COG
- GPS position (lat/lon) — mainly for confirmation
- Battery voltage / house bank status (if available)
- Engine hours or RPM (if present on the network)

**Nice-to-have later:**
- Water temperature
- Air temperature / barometric pressure
- AIS targets (summary only)
- Autopilot status (read-only)

## Architecture Choices

### Option A – Direct NMEA to Terminal (Recommended for v1)
- Terminal talks directly to the boat’s NMEA network
- Lowest latency, works completely offline
- Simplest software path

### Option B – Signal K Bridge
- Intermediate Signal K server normalises data
- More flexible for future multi-display or richer integrations
- Adds complexity and another component to maintain

**Proposal:** Start with **Option A**. Revisit Signal K only if the pilot demonstrates clear need.

---

## NMEA 2000 Bus Safety Requirements (Critical)

Connecting any new device to a boat’s NMEA 2000 backbone is the highest-risk part of this project. The backbone often carries data used by the autopilot, chartplotter, and engine systems. A poorly designed node can degrade or disable the entire segment.

### Mandatory Requirements Before Any NMEA 2000 Connection

1. **One ground, or isolation**  
   The terminal must never join the bus ground to a second ground. There are two accepted ways to connect:
   - **Bus-powered, single ground (primary).** The terminal takes all its power from NET-S / NET-C and has no other wired connection. Its only ground is the bus ground, so there is no ground loop and a non-isolated transceiver is acceptable. This is how bus-powered N2K sensors work.
   - **Isolated (when self-powered).** If the terminal keeps its own 12 V supply, or any other wired link (RS485, USB, 0183), the CAN interface must be galvanically isolated: an isolated CAN transceiver or a certified marine gateway.

2. **Clean failure mode**  
   If the terminal loses power, locks up, or is removed, it must drop off the bus cleanly. It must never load the bus, inject noise, or hold the bus in a dominant state.

3. **Load Equivalency Number (LEN)**  
   The interface must declare and respect a realistic LEN. The total LEN on any segment must stay within NMEA 2000 limits.

4. **Certified or high-quality drop cable**  
   Prefer a proper Micro-C drop cable and tee rather than DIY wiring. This reduces installation errors and improves reliability.

5. **Bus power only as a single-ground node**  
   A bus-powered terminal draws NET-S / NET-C through its own fuse and a DC/DC converter, declares its measured LEN (20 LEN, 1 A, maximum), and has no other wired connection while it is on the bus. Flashing and debugging happen off the bus or through an isolated USB adapter. A self-powered terminal must not draw bus power and must use the isolated method.

### Liability Note

Modifying a vessel’s instrument network has insurance and liability implications. Before any permanent installation on a club boat, the Board should be informed and, if appropriate, the club’s insurer consulted. This is not purely a technical decision.

### Pilot Recommendation

For the first pilot boat, prefer a vessel that already has clean NMEA 0183 outputs or a well-documented NMEA 2000 backbone with available drop points. Avoid boats where the network is poorly understood or heavily loaded until the boat-terminal connection has passed the bench test in [doc 19](19-n2k-backbone-install.md#5-bench-test-before-the-boat).

---

## Physical Connection Notes

- Installation requires identifying the existing talker (0183) or backbone (2000)
- Keep runs short
- Use proper shielding and strain relief
- Document the connection point and any LEN contribution for future maintainers

## Waveshare listen-only NMEA 2000 (firmware)

Building the boat backbone: see [19 – NMEA 2000 Backbone Install](19-n2k-backbone-install.md).

The ESP32-S3-Touch-LCD-7 has a TJA1051 on a PH2.0 header. That transceiver is **not isolated**, so how the board is powered decides how it may connect:

- **Primary: bus-powered.** NET-S / NET-C through a 1 A fuse and a 12 V to 5 V DC/DC into the board's USB socket. CAN-H / CAN-L to the header. Nothing else wired to the board: no USB lead, no RS485, no 12 V, no battery. Wi-Fi and BLE only. Estimated LEN is 10 until it is measured. The board turns off when the bus power does.
- **Alternative: isolated.** If the board must keep its own power or another wired link, fit an isolated CAN interface or a certified gateway between it and the tee. Never connect the bare TJA1051 to a backbone while the board has its own supply.

Wiring, DC/DC choice, LEN and bench test are in [doc 19](19-n2k-backbone-install.md#4-boat-terminal-connection). The board only accepts 5 V (USB-C); it has no wide-range DC input.

| Pin / control | Function |
|---------------|----------|
| GPIO20 | CANTX into the TJA1051. Also USB D+ when the mux is on USB. |
| GPIO19 | CANRX. Also USB D−. |
| CH422G EXIO5 | High selects CAN. Low selects USB. Default build leaves it low. |
| Jumper 13 | Onboard 120 Ω terminator. **Remove it** before any bus connection. One terminator at each physical end of the backbone is enough. |
| UART Type-C (GPIO43 / GPIO44, CH343) | Serial log once CAN owns GPIO19/20. Native USB CDC is gone until reboot in practice. Bench only: on a bus-powered board, nothing is plugged in here. |

`NMEA_SOURCE` in `firmware/src/secrets.h` (see `secrets.h.example`):

- `0` — simulator. USB stays up. This is the bench and CI default.
- `1` — NMEA 2000 listen-only.
- `2` — simulator until a CAN frame arrives, then the bus.

Runtime, on the live console: `NMEA SIM`, `NMEA N2K`, `NMEA AUTO`. Switching back to `SIM` releases the controller and tries to restore USB. If the USB socket does not return, reboot.

The node does not transmit. The TWAI controller is installed in listen-only mode, and the library mode is `N2km_ListenOnly`. Preferred source address 22 is stored for the library and is **not** sent as an ISO Address Claim. `CANSendFrame` does not call `twai_transmit`. On bus-off the driver is uninstalled, GPIO20 is an input with pull-up (TJA1051 TXD high is recessive), and a later retry is still listen-only. Power loss leaves TXD high-Z; the transceiver's own TXD pull-up holds the bus recessive. On a bus-powered board a blown fuse or dead DC/DC also unpowers the TJA1051, and an unpowered TJA1051 disengages from the bus with zero load (NXP datasheet). Its TXD dominant time-out also releases the bus if TXD is ever stuck low.

### PGN to NMEA 0183

Decoded in `firmware/src/n2k_to_0183.cpp` and pushed through the existing sentence sink, so TCP 10110 and BLE notify are unchanged. Instrument sentences are limited to about 1 Hz. AIS is forwarded as it arrives. Cached GNSS older than 3 s is dropped, not repeated, so a GPS that goes quiet stops sending a fix instead of freezing on its last one. Fields N2K reports as not available are left empty.

| PGN | Contents | 0183 |
|-----|----------|------|
| 126992 | System time | time fields on RMC / GGA |
| 129025 | Latitude / longitude | RMC / GLL cache |
| 129026 | COG / SOG | RMC / VTG cache |
| 129029 | GNSS position | RMC, GGA, GLL. GGA is only sent while 129029 is live, since its quality, satellites, and altitude come from it. "No fix" makes RMC / GLL status `V`. |
| 128267 | Depth | `$SDDPT` |
| 128259 | Water speed | `$VWVHW` |
| 130306 | Wind | `$WIMWV` for apparent (`R`) and boat-referenced true (`T`). Ground-referenced wind (true or magnetic north) is `$WIMWD`, because it is a compass direction, not an angle off the bow. |
| 127250 | Heading | `$HCHDG` if magnetic, otherwise `$HEHDT` |
| 129038 | AIS Class A position | `!AIVDM` / `!AIVDO` type 1 |
| 129039 | AIS Class B position | type 18 |
| 129040 | AIS Class B extended position | type 19 |
| 129794 | AIS Class A static and voyage | type 5 |
| 129809 | AIS Class B static, part A | type 24 A |
| 129810 | AIS Class B static, part B | type 24 B |
| 129808 | DSC call | `$PLMYC,DSC,<hex>` (raw payload, first 16 bytes) |

DSC goes out as a proprietary `$PLMYC,DSC` sentence, not an alarm. The firmware does not synthesize `$CDDSC` or `$CDDSE`. A wrong distress sentence would look like a real distress to Navionics, and a `$CDDSE` with hex in its MMSI field would be misread by plotters. Third-party apps ignore `$P` sentences. Own-ship AIS (transceiver information "own" or a transmission) is `!AIVDO`; everything else is `!AIVDM`. Talkers are `GP`, `SD`, `WI`, `HC` / `HE`, `VW`, and `AI`.

`make firmware-test` runs a host test on the Mac (no board): it feeds library-built PGNs through the converter, checks the 0183 sentences and checksums, checks the AIS packer against published `!AIVDM` reference sentences, and decodes every AIS sentence with an independent Python decoder (`firmware/test_host/ais_check.py`). It has not been run against a live NMEA 2000 bus.

The FIX chip stays `SIM` on the simulator, `N2K` when listen-only is selected but no frame has arrived, and `BOAT` while frames are flowing. `PHONE` still means gated GNSS inject.

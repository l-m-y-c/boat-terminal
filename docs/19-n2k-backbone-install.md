# 19 – NMEA 2000 Backbone Install

The club has approved an NMEA 2000 (N2K) backbone on the boat. This doc is the build plan: layout, parts, power, the boat-terminal drop, a bench test, and commissioning.

Firmware details live elsewhere. See [05 – NMEA Connectivity](05-nmea-connectivity.md) for bus safety and the PGN table, and [firmware/README.md](../firmware/README.md) for build and flash.

**Status:** draft. Nothing here has been installed or tested on a live bus yet.

## 1. Topology

N2K is one straight line. Tees sit end to end along the backbone. Each device hangs off the top of a tee on a drop cable. A 120 Ω terminator closes each physical end. Power comes in through one fused tap.

```mermaid
flowchart LR
    TF[Female terminator<br/>120 Ω] --- T1
    T1[Tee] --- T2[Tee] --- TP[Power tee] --- T3[Tee] --- T4[Tee] --- T5[Spare tee]
    T5 --- TM[Male terminator<br/>120 Ω]

    T1 -. drop .- VHF[Garmin Signal VHF 400<br/>+ VHF/AIS antenna]
    T2 -. drop .- DEP[Airmar DST810 depth sounder]
    TP -. power cable .- PWR[Switched 12 V<br/>3 A fuse]
    T3 -. drop .- PLOT[Garmin GPSMAP 740s]
    T4 -. drop .- DCDC[1 A fuse + diode + Pololu APM81815<br/>from NET-S / NET-C] --- BT[boat-terminal<br/>Waveshare, bus-powered, N2K node]

    PLOT == Garmin Marine Network ==> RADAR[Radar]
    BT -. WiFi .- EINK[e-Ink display]
    BT -. WiFi / BoatLink .- PHONE[Member phones]
```

Only the solid line between the terminators is the N2K bus. The radar, e-Ink display and phones are not on it.

Rules:

| Rule | Value |
|------|-------|
| Terminators | Exactly two, one male and one female, at the physical ends. None in devices. |
| Drop cable | 6 m max each. |
| Total drop length | 78 m max (Micro/Light cable). |
| Backbone length, Micro/Light cable | 100 m max. |
| Backbone length, Mid cable | 100 m max. Mid only helps with power, not length. |
| Backbone length, Heavy cable | 200 m max. Not needed on this boat. |
| Power tap | One. Near the middle of the load. Switched, not straight to the battery. |
| Backbone cables | Side to side between tees only. Never into the top of a tee. |

Order the tees to match where things are mounted. The order in the diagram is a placeholder.

The spare tee lets the club add a wind sensor later without opening the backbone.

The drop, backbone and Garmin rules come from Garmin's NMEA 2000 technical reference ([glossary](https://www8.garmin.com/manuals/webhelp/GUID-1415AAD0-FE63-42A6-8F8D-DB713D616122/EN-US/GUID-9C47C97C-88A2-4EC6-9846-9B6937C3ACA7.html)). The 78 m total drop and the Heavy cable figures are the usual NMEA 2000 limits. They are not yet confirmed from a primary source, so check them against the Garmin reference before buying long runs.

## 2. Parts list

| Qty | Part | Part number | Notes |
|-----|------|-------------|-------|
| 1 | Garmin NMEA 2000 Starter Kit | 010-11442-00 | Two tees, male and female terminators, 2 m drop, 6 m backbone, 2 m power cable. [Garmin](https://garminssa.com/ghana/010-11442-00-nmea-2000-starter-kit.html), [The GPS Store](https://www.thegpsstore.com/Marine-Electronics/NMEA-2000-Components/Garmin-NMEA-2000-Starter-Kit). One reseller lists a 10 m backbone; most say 6 m. |
| 3–4 | Extra Micro-C tees | TBD | Total needed is five (four devices plus power), plus one spare. The kit gives two. |
| As measured | Micro-C backbone cable | TBD | Length from the mounting plan. |
| 0–2 | Micro-C drop cables | TBD | The VHF and 740s may come with one. Check what's on the boat. |
| 1 | Micro-C drop, bare wire one end, or field-attach connector | TBD | For boat-terminal. Power and data wires are used. The drain is cut back. |
| 1 | 5 V step-down regulator carrier | [Pololu APM81815, 5V Out](https://www.pololu.com/product/5267) (#5267), US$6.95 | 12 V to 5 V for boat-terminal. Non-isolated, 5.05–72 V in. No reverse protection, hence the diode below. The [D36V28F5](https://www.pololu.com/product/3782) (#3782, US$23.95) is a pricier alternative with it built in. See [20](20-n2k-bus-power-supply.md). |
| 1 | Waterproof inline ATO/ATC fuse holder | [Blue Sea 5065](https://www.bluesea.com/products/5065/Waterproof_In-Line_ATO_ATC_Fuse_holder) | On NET-S in the boat-terminal drop, before the Pololu. |
| 1 + spares | 1 A ATO blade fuse, fast-acting | Littelfuse 0287001.PXCN | For the holder above. |
| 1 | USB-C male to 2 bare wires power pigtail (VBUS and GND only) | Generic 22 AWG | Pololu output into the board's **USB** socket. No CC resistor needed (Waveshare schematic V1.2). Check polarity with a meter. |
| 0–1 | 40 V 3 A Schottky diode, recommended | onsemi 1N5822 (SS34 is the SMD equivalent) | In series in NET-S, band towards the Pololu VIN. Reverse-polarity protection. |
| 0–1 | Bidirectional TVS | Littelfuse P6KE27CA | Fit with the diode, on its bus side. Clamps spikes at about 37 V. Optional if there's no diode. |
| 0–1 | 100 µF 50 V electrolytic, optional | Panasonic EEU-FC1H101 | Fit only if the hot-plug test shows VIN over about 40 V. See [20](20-n2k-bus-power-supply.md#bench-test). |
| 1 | Small IP66/67 box with two cable glands | generic | For the Pololu and the drop splices. |
| 1 | 3D-printed case for the display board, optional | [Printables model 1558735](https://www.printables.com/model/1558735-waveshare-esp32-s3-touch-lcd-7-case) | See the note below. |
| 1 | Inline fuse holder and 3 A fuse | — | For the power tap. Usually on the kit power cable. Confirm. |

The full boat-terminal power parts list, with specs and links, is in [bom.csv](assets/n2k-power/bom.csv).

**Display case.** The [Waveshare ESP32-S3-Touch-LCD-7 case](https://www.printables.com/model/1558735-waveshare-esp32-s3-touch-lcd-7-case) on Printables (by Bill Kater, CC BY-SA) is a one-piece, wall-mount print with keyholes for hanging it several ways. Files come as STL, 3MF, F3D and OBJ. It is for the original ESP32-S3-Touch-LCD-7, not the 7B. It encloses the display board only. Openings for the N2K drop / CAN entry and the USB-C lead are drilled after printing and sealed with Sikaflex-291. Whether the Pololu, the fuse and the drop splice fit inside it hasn't been checked, so plan on the separate small box.

Part numbers marked TBD were not verified in this pass. No prices are listed; the ones seen online ranged widely by reseller.

The prototype connection below has no isolated CAN interface. That is an open compliance item, not a design choice. See section 4.3.

## 3. LEN / power budget

LEN is the load each device puts on bus power. 1 LEN = 50 mA. A device may draw at most 20 LEN (1 A) from the bus ([NMEA](https://web.nmea.org/External/WCPages/WCWebContent/webcontentpage.aspx?ContentID=391)).

| Device | LEN | Source |
|--------|-----|--------|
| Garmin Signal VHF 400 | 1 (50 mA) | [Install specs](https://www8.garmin.com/manuals/webhelp/GUID-60E76EAD-A560-449E-A528-6C89D9026DB3/EN-US/GUID-FD8D10A4-78DB-4E5F-A87F-59A95E979777.html) |
| Garmin GPSMAP 740s | 2 (100 mA max) | [700 series install specs](https://www.manualsdir.com/manuals/107866/garmin-gpsmap-190-01155-02-gpsmap-700-series-gpsmap-740s.html?page=8) |
| Airmar DST810 thru-hull (chosen) | 3 | [Airmar DST810](https://www.airmar.com/Catalog/Marine/NMEA-SMART-Sensors/DST810-LSP) |
| boat-terminal (bus-powered) | **10 (500 mA)** | Estimate. See below. Replace with the bench measurement. |
| **Total** | **16 (800 mA)** | |

The VHF and the 740s also have their own main power. Their LEN is only the N2K interface.

Sixteen LEN on a 3 A tap is inside the limits. boat-terminal's 10 LEN is under the 20 LEN per-device maximum. With Micro/Light cable the usual planning limit is about 3 A per side of the power tap, so a centred tap still leaves room. boat-terminal is the biggest load, so put its tee close to the power tee.

### How boat-terminal's 10 LEN was worked out

Waveshare publishes one figure for the ESP32-S3-Touch-LCD-7: **5 V, 450 mA**, with input "Type-C 5 V" ([wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7), [docs](https://docs.waveshare.com/ESP32-S3-Touch-LCD-7)). It doesn't say whether that is with the backlight full or Wi-Fi sending, so the rest is an estimate.

- The backlight driver is an AP3032 boost from 5 V ([schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-7/ESP32-S3-Touch-LCD-7-Sch.pdf), V1.2). Its 1.8 Ω sense resistor and 200 mV reference ([AP3032 datasheet](https://www.diodes.com/assets/Datasheets/AP3032.pdf)) set about 110 mA of LED current at full brightness.
- Wi-Fi transmit peaks at 340 mA on the ESP32-S3, against about 90 mA when receiving ([ESP32-S3 datasheet](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf), table 4-7). The 3.3 V rail comes from a linear regulator, so that peak shows up as about 250 mA extra at 5 V.
- Design figure (estimate): 450 + 250 = **700 mA at 5 V, 3.5 W**.

At the bus, through a DC/DC at 85 % efficiency (a cautious figure; the Pololu does better, see below):

| Case | Power in | At 12 V | At 9 V |
|------|----------|---------|--------|
| Waveshare figure, 450 mA at 5 V | 2.6 W | 220 mA | 295 mA |
| Design peak, 700 mA at 5 V (estimate) | 4.1 W | 345 mA | 460 mA |

LEN uses the worst case: 460 mA at 9 V, rounded up to 10 LEN.

With the chosen Pololu APM81815 (about 92 % at 0.7 A from Pololu's efficiency graph), the current at the tee is **about 0.44 A at 9 V, or 0.46 A with the reverse-polarity diode fitted**. That includes the fuse and a 6 m drop. Even at 87 % it is 0.47 A, or 0.49 A with the diode. All are inside 10 LEN (0.50 A). See [20](20-n2k-bus-power-supply.md).

If the board measures above about 0.75 A at 5 V, the 9 V bus current passes 0.5 A and the LEN goes to 11–12. The total would then be 17–18 LEN, still inside the 3 A tap. Measure on the bench (section 5) and update this table before commissioning.

The board also has a CS8501 lithium charger on its PH2.0 battery header, set to 580 mA. **Leave the battery header empty.** A battery would add up to 580 mA of charging load on the bus.

Airmar says the DST810 is being replaced by the DST820 by mid-2026. If the club buys new, check the DST820 sheet.

## 4. boat-terminal connection

### 4.1 Primary: powered from the bus

boat-terminal takes its power and its data from one drop cable. Its only ground is the bus ground, NET-C. With one ground there is no ground loop. This is how the prototype goes on the bus until it has an isolated CAN interface. The onboard TJA1051 and the Pololu are both non-isolated, and a compliant N2K device is isolated from the network (section 4.3).

The board only accepts 5 V, on its USB-C sockets. There is no wide-range DC input on this board. So a DC/DC converter, the Pololu APM81815, turns bus power into 5 V. The full design, parts and power bench test are in [20 – N2K Bus Power Supply](20-n2k-bus-power-supply.md).

![boat-terminal N2K bus power wiring](assets/n2k-power/schematic.png)

The same diagram as [SVG](assets/n2k-power/schematic.svg).

```mermaid
flowchart LR
    TEE[Tee] --- DROP[Micro-C drop]
    DROP -- "NET-S (red)" --> FUSE[1 A ATO fuse<br/>Blue Sea 5065] --> DCDC[Diode + Pololu APM81815<br/>12 V to 5 V, non-isolated]
    DROP -- "NET-C (black)" --> DCDC
    DCDC -- "5 V + GND,<br/>2-wire USB-C pigtail" --> USB[Waveshare UART socket]
    DROP -- "CAN-H (white) / CAN-L (blue)" --> CAN[Waveshare CAN header<br/>PH2.0, jumper 13 off]
    DROP -. "shield / drain (bare):<br/>cut back, insulate" .- X[not connected]
```

Wiring:

1. Remove jumper 13, the onboard 120 Ω terminator.
2. NET-S (red) through the Blue Sea 5065 holder with a 1 A ATO fuse (Littelfuse 0287001.PXCN), then the 1N5822 diode (band towards the Pololu), to the Pololu VIN. The Pololu has no reverse protection, so if you leave the diode out, check the drop polarity with a meter before every first connection.
3. NET-C (black) to the Pololu GND. With the diode, fit the P6KE27CA TVS from NET-S to NET-C on the bus side of the diode. Fit the 100 µF cap across the Pololu VIN/GND only if the hot-plug test needs it.
4. Pololu VOUT and GND to the board through a 2-wire USB-C power pigtail into the socket marked **UART**. The board takes power only through its USB-C sockets, and in NMEA 2000 mode the **USB** socket's pins (GPIO19/20) are the CAN bus, so the USB socket is kept free of any cable. The UART socket feeds the 5 V rail through its own P-FET; check the pigtail polarity with a meter first. Leave EN open. Leave the USB socket empty.

   The UART socket is also the only console in NMEA 2000 mode (CH343, 115200). To debug on the boat, unplug the pigtail and plug in a laptop on battery instead: the laptop then powers the board. See [the boat test](boat-test-2026-10-28.md). (Before 2026-10-27 this step said the USB socket. A strictly 2-wire pigtail never touches the data pins, so the USB socket also works electrically; the UART socket was chosen so no data cable ever goes on the CAN pins.)
5. CAN-H (white) and CAN-L (blue) to the board's CAN header (PH2.0, High and Low). Check colours against the cable's own sheet.
6. Shield / drain (bare): cut it back at the boat-terminal end and insulate it. On N2K the shield is grounded at one point only, the power tap. Devices don't connect it.
7. On the terminal, tap the **SIMULATION** badge and choose **NMEA 2000**. The choice is saved, so do it once, on the bench, before the drop goes on. (`NMEA_SOURCE 1` in `firmware/src/secrets.h` does the same for a freshly erased board.) See [05](05-nmea-connectivity.md#data-source-simulator-or-nmea-2000).

The Pololu is non-isolated, so its input and output grounds are already common. That matters: the CAN header has no ground pin. The TJA1051 gets its bus reference through the board ground, so board ground must be NET-C.

The Pololu is a bare 10 × 19 mm board with no mounting holes. Solder the wires straight to its pins, hold it down with foam tape or sealant in a small IP66/67 box with the drop splices, and conformal-coat it. Keep the fuse reachable.

**The hard rule: no other wired connection while it's on the bus.**

- No USB lead to a laptop on mains or ship power.
- No RS485 or NMEA 0183 wire.
- No separate 12 V feed.
- No battery on the PH2.0 header.
- Wi-Fi and BLE only.

**Isolation:** the board is not isolated from the bus. While the terminal is on the backbone, never plug a laptop that is on mains or ship power (or charging) into the UART socket: battery only, charger unplugged, nothing else connected to the laptop.

Any second wire brings back a second ground, and with it the ground loop. The same goes for a phone charger or a laptop on mains touching the board. Flash and debug the board off the bus, or from a laptop running on its own battery with nothing else plugged in.

boat-terminal turns off whenever the N2K power is switched off. That's expected. The helm display and the phone link come back when the bus does.

If the fuse blows or the DC/DC fails, the TJA1051 has no power. It then disengages from the bus with zero load ([TJA1051 datasheet](https://www.nxp.com/docs/en/data-sheet/TJA1051.pdf)). The other devices see the terminal's address go quiet, as with any device that is switched off.

### 4.2 Isolated connection

This is the target for a compliant device, and the only way if boat-terminal must keep its own power or needs another wired link while it's on the bus (for example an RS485 0183 instrument, or a permanent USB lead). In those cases the board has two grounds, so the bus side must be isolated.

- **Isolated CAN transceiver in place of the onboard TJA1051.** A TI ISO1042 / ISO1050 class part, or an ADI ADM3053 (which also isolates power), with isolated power for the bus side. Ready-made modules such as the [M5Stack Unit CAN](https://docs.m5stack.com/en/unit/Unit_CAN) or the [MikroE CAN Isolator Click](https://www.mikroe.com/can-isolator-click) can be wired to GPIO43/44 on the UART header, with the onboard TJA1051 left disconnected. It's a pin change in `n2k.cpp` and avoids the USB/CAN pin swap. It takes over the UART log pins. It must pass the bench test below. Its bus side must not be terminated. If it draws from NET-S, count its LEN.
- **Actisense NGW-1 gateway into RS485.** The [NGW-1](https://actisense.com/products/nmea-2000-gateway-ngw-1/) is a certified, isolated N2K to 0183 gateway. Its 0183 output goes into the board's RS485 header. Nothing home-made touches the bus, and the terminal is then not an N2K node at all. It needs new firmware to read 0183 from RS485, and the N2K code becomes a fallback.

Waveshare's isolated CAN HATs ([2-CH CAN HAT](https://www.waveshare.com/2-ch-can-hat.htm)) are SPI controllers with their own MCP2515. They don't fit the ESP32's built-in CAN controller and are **not** a recommendation here.

### 4.3 Compliance open items

The firmware makes boat-terminal a standard NMEA 2000 node ([05](05-nmea-connectivity.md#the-terminal-as-an-nmea-2000-node)). Two things are not met by the prototype and cannot be fixed in firmware. Both must be resolved before it is a fully compliant device.

1. **Electrical isolation.** The library author's certification checklist requires the device to be electrically isolated from the network. The onboard TJA1051 is not isolated, and the Pololu APM81815 is not isolated. Resolution: an isolated CAN transceiver with isolated power for the bus side (section 4.2) instead of the onboard TJA1051. Until then, never connect anything else that shares ground (laptop on mains, phone charger, USB) while boat-terminal is on the backbone.
2. **NMEA-issued codes and certification.** The manufacturer code and product code must be issued by NMEA, which needs NMEA membership. The library author quotes about $1,200 for a manufacturer code and $450 for a product code (2024 member prices). Certification testing and verification cost extra; his total estimate for a new member is about $4,000. Until then boat-terminal uses the open-source manufacturer code 2046. It behaves to the standard, but it is not certified.

## 5. Bench test before the boat

Do this on a table, not on the boat.

Bus:

- Two terminators, one power tee with a fused bench 12 V supply (adjustable, so it can go down to 9 V), two or three tees.
- One known N2K talker (the 740s, the VHF, or a spare GPS) **or** a USB-CAN adapter running a simulator.
- A sniffer (USB-CAN adapter with candump or a similar tool) to watch the bus.
- A multimeter or USB-C power meter for current.

Flash and check the log with the board **off** the bus, on USB from the laptop. Unplug the laptop from the board before the drop goes on. The sniffer can stay on the laptop. Never have the laptop plugged into the board and the drop on at the same time.

Checks:

| # | Check | Pass |
|---|-------|------|
| 1 | `make firmware-test` on a Mac | All checks pass. |
| 2 | `make firmware` builds | No errors. |
| 3 | Off the bus: choose **NMEA 2000** on **Data source** and confirm. Watch the UART Type-C log. | The terminal restarts. CAN comes up. EXIO5 goes high. USB CDC drops as expected. The badge reads **NMEA 2000 NO DATA**. |
| 4 | First run the power-supply bench test in [20](20-n2k-bus-power-supply.md#bench-test). Then unplug the laptop and power the board from the drop through the fuse, diode and Pololu. | Board boots. Screen and Wi-Fi come up. Nothing else is wired to it. |
| 5 | Current at 12 V into the drop, backlight full, Wi-Fi active (phone streaming TCP 10110) | Note the peak. Update the LEN in section 3. Must stay under 1 A (20 LEN). |
| 6 | Turn the bench supply down to 9 V | Board keeps running, no resets. Note the current at 9 V. |
| 7 | Pin handover on GPIO19/20 | Frames count up on the FIX chip or phone. This has never been tested on hardware. |
| 8 | Sniffer filtered on boat-terminal | Only network-management PGNs from it: an address claim (60928) at start, a heartbeat (126993) every 60 s, and answers to requests (126996 product information shows LEN 10). **No** instrument-data PGNs. No error frames when it joins or leaves. |
| 8a | Address contention: from the sniffer, send an address claim for boat-terminal's address with a NAME that wins (lower value) | boat-terminal moves to a free address and the **Data source** screen shows the new address. After a restart it comes back on that new address. |
| 9 | Pull boat-terminal's drop while the bus runs | Other traffic carries on with no errors. |
| 10 | Pull the fuse while the bus runs | Same. The unpowered TJA1051 loads nothing. |
| 11 | Phone on BoatLink, TCP 10110 | RMC, DPT and AIVDM sentences arrive. |
| 12 | FIX chip | Shows BOAT while frames flow. |

Test switching to the simulator and back on **Data source** off the bus, where USB is reachable. Each switch restarts the terminal.

The terminal acknowledges frames like every powered N2K device, so a bench bus with one sender and the terminal works without a third node. If **Data source** shows the `repeats` hint, some device is not getting acknowledgements: check wiring and termination.

While frames flow, open **Data source** on the glass, or **NMEA data** on the phone. **Lost frames** must stay at 0, and the buffer peak should stay far below 2048. `NMEA STATS` on the UART console prints the same counters.

## 6. Commissioning on the boat

| # | Check | Pass |
|---|-------|------|
| 1 | Bus power off. Ohms between CAN-H and CAN-L at any tee. | About 60 Ω. 120 Ω means a terminator is missing. 40 Ω means one too many. |
| 2 | boat-terminal: nothing wired but the drop | No USB, RS485, 12 V or battery lead on the board. Jumper 13 off. |
| 3 | Bus power on, everything running, boat-terminal backlight full. Volts NET-S to NET-C at the far end from the tap. | At least 9 V under load, ideally close to battery. |
| 4 | Same reading at boat-terminal's drop | At least 9 V. |
| 5 | 740s device list | VHF 400, depth sounder and **LMYC Boat Terminal** listed. No address conflict shown. Note boat-terminal's address from its **Data source** screen. |
| 6 | Depth on the 740s | Reads and matches a known depth. |
| 7 | VHF GPS and AIS on the 740s | Position and AIS targets show. |
| 8 | boat-terminal FIX chip | Shows BOAT. |
| 9 | Phone over BoatLink | Depth, position and AIS targets show. |
| 10 | Switch bus power off and on | boat-terminal goes dark and comes back by itself. Everything else comes back without a reboot. |

Write the as-built order of tees, cable lengths, the measured boat-terminal current and the LEN total into the boat's maintenance book.

## 7. Open questions

- **Isolation.** The prototype is not isolated from the bus (section 4.3). Choose and fit an isolated CAN transceiver with isolated bus-side power before calling the install compliant.
- **NMEA codes.** The manufacturer code (open-source 2046) and product code are not NMEA-issued, and the terminal is not certified (section 4.3). Whether to pay for NMEA membership, codes and certification is still open.
- **boat-terminal current.** The 10 LEN is an estimate. Measure it on the bench (section 5, checks 5 and 6).
- **Power box and display case.** The Pololu, diode, TVS, fuse and splices need a small box near the display. Whether they fit inside the Printables display case hasn't been checked. Openings in the case for the drop and USB-C lead are drilled after printing and sealed with Sikaflex-291.
- **Hot-plug spike.** Whether the optional 100 µF cap is needed depends on the hot-plug scope test in [20](20-n2k-bus-power-supply.md#bench-test).
- **Mounting positions and cable lengths.** Needed before ordering backbone cable and extra tees.

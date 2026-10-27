# 21 – First Boat Run: Garmin 740s on NMEA 2000

A checklist for the first time the boat terminal joins a real backbone. The expected bus is a Garmin GPSMAP 740s plus the terminal, and nothing else. The terminal joins as a standard NMEA 2000 node: it claims an address and answers the plotter, and it sends no instrument data. Each step says what you should see and what to do if you don't.

How the terminal behaves as a node, its diagnostics and the PGN table are in [05 – NMEA Connectivity](05-nmea-connectivity.md#the-terminal-as-an-nmea-2000-node). Wiring and bus power are in [19](19-n2k-backbone-install.md) and [20](20-n2k-bus-power-supply.md).

## 1. Before leaving the bench

- [ ] Terminal flashed with this firmware. On the simulator, the Data source screen's BUS card reads "Simulated".
- [ ] Phone app installed from the same commit. **NMEA data → Bus check** opens and shows six simulated devices.
- [ ] On the phone, **Bus check → Record bus** for 30 s, then **Stop → Share recording**. The file opens and has `can0` lines. This proves the capture path before you need it.
- [ ] A laptop with a USB-C cable for the terminal's **UART** Type-C socket (the CH343). It is the only log port once the terminal is on NMEA 2000, and opening it does not reset the board. 115200 baud. **Only on its own battery**, see the isolation item below.
- [ ] On the laptop, with the cable in the **UART** socket: `make board-status` says `app`, and `make diag` prints the summary. Try `make session` for a minute, then Ctrl-C. That is the boat mode, see [tools/README.md](../tools/README.md#on-the-boat).
- [ ] **Isolation.** The prototype's CAN interface and its 12 V to 5 V supply are not isolated (an open hardware item, see [05](05-nmea-connectivity.md#hardware-compliance-open-items)). While the terminal is on the backbone, nothing else that shares ground is connected to it: no laptop on mains or charging, no phone charger, no USB lead, no RS485, no second 12 V, no battery on the board. If you need the console, use a laptop running on its own battery with nothing else plugged in.

## 2. On the 740s, before connecting

Write down what the plotter is set to. Each of these changes what the terminal shows.

- [ ] **Keel offset** (vessel / sonar settings). The 740s should send it as the offset in PGN 128267. The terminal's depth tile then reads "below keel" for a negative offset or "below surface" for a positive one. If it still says "below transducer", the offset isn't reaching the bus.
- [ ] Heading reference: true or magnetic. Magnetic COG becomes true only once a variation (127258) arrives.
- [ ] Whether the 740s is set to send its internal GPS on NMEA 2000.
- [ ] Open the 740s's NMEA 2000 device list now and note what it shows without the terminal.

## 3. Wiring and power

- [ ] Backbone powered from its own fused 12 V tap. The 740s's NMEA 2000 port is powered by the bus, not by the plotter's power cable.
- [ ] One 120 Ω terminator at each end of the backbone. The terminal's onboard terminator jumper is **open**.
- [ ] Terminal powered as described in [doc 19](19-n2k-backbone-install.md#4-boat-terminal-connection).
- [ ] Optional check with the bus powered and everything off: CAN-H to CAN-L reads about 60 Ω.

## 4. Joining the bus

1. On the terminal, **Data source → Use NMEA 2000 → Switch and restart**.
2. Turn the 740s on. Watch the **BUS** card on the Data source screen and the phone's **NMEA data**.
3. The node state on the Data source screen goes from "claiming an address" to **"NMEA 2000 node, address N"**. Write down N.
4. On the 740s, open the NMEA 2000 device list. It should now show **LMYC Boat Terminal**. That is the check that the terminal has joined the bus.
5. Check the 740s shows no address conflict or bus warning.

What you see, and what to do:

| Data source screen says | Meaning | Do |
| --- | --- | --- |
| **NMEA 2000 node, address N**, no hint | Joined. Frames from the 740s are arriving. | Go to section 5. |
| *Claiming an address* for more than a few seconds | The claim has not finished. | Wait 10 s. If it stays, record 30 s of the bus and go back to the bench. |
| *No free address* (`no_address`) | The terminal could not claim an address. | Note the 740s's device list, record 30 s of the bus, and report it. |
| *The same frame keeps repeating…* (`repeats`) | Some device is not getting acknowledgements. | Check wiring and termination. |
| *Bus errors and no frames…* (`errors_no_frames`) | Error frames, nothing good. | Check CAN-H/CAN-L polarity, both terminators, and bus power. |
| *The terminal went bus-off and is rejoining…* (`bus_off`) | Its own frames kept failing, so the controller left the bus; standard recovery is running. | Check CAN-H/CAN-L and both terminators. |
| *No frames…* (`no_frames`) | Silence. | Check the drop cable, 12 V on the backbone, and that the 740s is on. |

## 5. What to check with data flowing

On the phone, open **NMEA data → Bus check**:

- [ ] **Devices.** Write down each source address and name. The 740s may show as more than one address (for example plotter and sonar). None of them uses the terminal's address from section 4.
- [ ] **Values and the device they come from.** Position, course & speed, time, depth, sea temperature and variation each have an owner. Any amber line ("… also sends it") means two devices send the same value. Note which addresses.
- [ ] **PGNs heard.** Note every PGN, its rate, and its source. For fast-packet PGNs (129029 and friends), "whole" should equal the starts. An amber row means frames are going missing.
- [ ] **Converter.** "bad" should be 0. If it isn't, note which PGN: the 740s is sending error codes there.
- [ ] Instruments on the phone and the terminal agree: depth (and its "below …" label), SOG, COG.
- [ ] Tie up and sit still: SOG reads **0.0**, not "—", and COG may be blank.
- [ ] Compare against the 740s's own screen: depth (with keel offset), SOG, COG, position, water temperature.
- [ ] Navionics on the phone still shows position and depth.
- [ ] Restart the terminal (bus power off and on, or the Data source switch and back). It comes back on the same address, and the 740s still lists **LMYC Boat Terminal** with no conflict.

## 6. Captures to bring home

Each capture is a `lmyc-bus-*.log` file in candump format. canboat's `analyzer` reads it, and it can be turned into a host test. Record each one with **Record bus → Stop → Share recording**, and also share **Share this bus check** (the JSON) at the same moment.

1. Plotter booting with the terminal on the bus: start recording, then turn the 740s off and on. This catches the address claims of both devices and the product information.
2. At the dock, 2 minutes.
3. Under way for 5 minutes, if possible.

With a laptop on the UART socket (on its own battery), keep `make session` running for the whole visit: it saves the log and a diagnostics snapshot every 10 s. Check each recording on the spot with `make capture-summary CAP=<file> TERMINAL=<address>`: it should say the terminal sent only network-management PGNs.

## 7. Back at the bench

- `make replay CAP=<file>` runs each capture through the real NMEA2000 library, diagnostics and 0183 converter on the host, giving `<file>.0183.txt` and `<file>.n2k.json`. Keep the captures: they become regression tests.
- Check capture 1 in canboat's `analyzer`: the terminal's address claim, NAME and product information decode as expected, and the 740s and the terminal end up on different addresses.
- Add the 740s's real PGN list and rates to [doc 05](05-nmea-connectivity.md).
- Note anything the 740s showed about the terminal in its device list (name, model version, software version) in [doc 05](05-nmea-connectivity.md).

# 20 – N2K Bus Power Supply

This is how boat-terminal takes its power from the NMEA 2000 drop. It uses only bought parts. There is no custom PCB.

It is the detail behind [19 – NMEA 2000 Backbone Install](19-n2k-backbone-install.md), section 4.1.

**Status:** draft, 2026-10-18. Nothing here has been built or tested yet.

Files:

| File | What it is |
|------|------------|
| [schematic.png](assets/n2k-power/schematic.png) | Wiring diagram |
| [schematic.svg](assets/n2k-power/schematic.svg) | Same diagram, scalable |
| [schematic.py](assets/n2k-power/schematic.py) | schemdraw script that draws both. Run `python3 schematic.py`. |
| [bom.csv](assets/n2k-power/bom.csv) | Parts list with part numbers, specs and links |

![boat-terminal N2K bus power wiring](assets/n2k-power/schematic.png)

## What it does

It powers the Waveshare ESP32-S3-Touch-LCD-7 from the N2K drop (NET-S / NET-C) with no custom PCB:

`NET-S → inline 1 A ATO fuse → Pololu D36V28F5 VIN` · `NET-C → Pololu GND` · `Pololu 5 V / GND → USB-C power pigtail → Waveshare "USB" socket` · `CAN-H / CAN-L → CAN header` · shield cut back.

Optional: a P6KE27CA TVS and/or a 100 µF electrolytic, soldered straight onto the Pololu VIN/GND pins.

## Is the Pololu D36V28F5 suitable? Yes.

All figures are from [pololu.com/product/3782](https://www.pololu.com/product/3782) unless noted.

| Need | Pololu D36V28F5 | Verdict |
|---|---|---|
| 9–16 V bus, ~8 V cranking dips | 5.3–50 V in. Dropout at 0.7 A is about 0.9 V (dropout graph), so 5 V holds down to about 6 V in | OK |
| Transients | "Voltages up to 50 V can be applied to VIN." No separate transient rating is published. The buck IC on the board is a Vishay SiC473 (visible on the board photo), rated 55 V operating and 60 V absolute max ([SiC47x datasheet](https://www.vishay.com/docs/75786/sic47x.pdf)) | Treat 50 V as the limit |
| Reverse polarity | Built-in reverse-voltage protection to 40 V. The bus can only reverse to −16 V | OK, nothing extra needed |
| Soft-start / inrush | "Soft-start feature limits inrush current and gradually ramps output voltage." No ramp time is published | Covers the output side (Waveshare 5 V rail has ≥100 µF) |
| Input capacitance | Not published. The board has on-board ceramics only | See hot-plug below |
| Quiescent | 2–3 mA no load (graph), negligible | OK |
| Output | 5 V ±4 % (max 5.2 V, under the Waveshare's own 5 V TVS), 3.2 A typical max | 4.5× headroom over 0.7 A |
| Size | 17.8 × 20.3 × 8.8 mm, two M2 holes | Fits a drop box |

### Efficiency and bus current at 5 V, 0.7 A out (3.5 W)

The efficiency figures are read from Pololu's D36V28F5 efficiency graph, which plots 6, 12, 24, 36 and 48 V curves. The 9 V and 16 V values are interpolated.

| Bus | η (graph) | Pololu input | Module loss |
|---|---|---|---|
| 9 V | ~94 % (interp. 6 V 96 % / 12 V 92 %) | 0.41 A | 0.22 W |
| 12 V | ~92 % | 0.32 A | 0.30 W |
| 16 V | ~91 % (interp. 12 V / 24 V) | 0.24 A | 0.35 W |

**At 9 V, the current at the tee is about 0.43 A.** That includes the fuse (0.123 Ω) and an assumed 6 m drop of about 0.64 Ω loop resistance (assumes about 22 AWG power conductors; check the cable sheet). Even if efficiency falls to 87 %, it is 0.46 A. **Both are inside the declared 10 LEN (0.50 A).** The 0.7 A load itself is still an estimate ([19](19-n2k-backbone-install.md#3-len--power-budget), section 3), so measure it on the bench. The module dissipates at most about 0.35 W, so it needs no heatsink.

## Front-end

**Fuse:** a Blue Sea 5065 waterproof inline ATO/ATC holder with a Littelfuse 0287001.PXCN 1 A blade, on NET-S as close to the tee end as practical. It sees 0.43 A steady, 43 % of its rating. The ATOF is fast-acting, but its melting I²t is 0.4 A²s. Hot-plug inrush is about 0.01 A²s even with C1 fitted (≈13 A peak, ~100 µs τ), so it won't nuisance-blow. The holder has 12 AWG pigtails, so join them to the thinner drop wires with marine adhesive-lined splices.

**TVS (D1, optional):** a P6KE27CA, bidirectional, axial, soldered across the Pololu VIN/GND pins.
- Vwm 23.1 V is above the 16 V bus, so it stays off in normal running and survives a 24 V jump-start.
- Its clamp is 37.5 V at 16.3 A, below the Pololu's 50 V limit and the 40 V reverse limit.
- Being bidirectional, it doesn't conduct on a −16 V reversal, so it doesn't fight the Pololu's reverse protection or blow the fuse.
- It's optional because the Pololu already takes 50 V and the backbone is fed from a switched, fused tap. It's cheap insurance against spikes from switching inductive loads.
- It will **not** survive an unsuppressed alternator load dump (ISO 7637 pulse 5a, around 80–100 V for hundreds of ms). That relies on the boat's alternator or regulator being clamped, as is normal.
- If it fails, it fails short, and then the fuse opens.

**Input cap (C1, optional):** Pololu does **not** require an input cap for this module. Pololu's [LC voltage spike app note](https://www.pololu.com/docs/0J16/all) warns that hot-plugging a board with ceramic input caps through long leads can ring up to about 2–3× the supply. Their remedy is a large, higher-ESR electrolytic in parallel.
- On a 12–16 V bus, 2× is 24–32 V, which is under the Pololu's 50 V. So start without C1.
- Fit the 100 µF / 50 V EEU-FC1H101 across VIN/GND only if the hot-plug scope test shows VIN over about 35 V.
- The trade-off is that C1 adds a ~100 µs, ~13 A charging pulse on the bus at plug-in. That is ordinary for an N2K device and well under the fuse I²t.

**Inrush limiting:** none added.
- The Pololu soft-start handles the big load, the Waveshare's 5 V bulk.
- The input-side pulse is only the module's small ceramics (plus C1 if fitted).
- An NTC would cost about 0.4 V at 9 V, wouldn't reset in a warm box, and is unnecessary.

## Output side (checked against the Waveshare schematic V1.2)

- The **"USB"** Type-C socket's VBUS pins go **directly to the board's 5 V rail**. CC1/CC2 each have **5.1 kΩ Rd to GND** (R62/R63). There is no PD or CC controller and no load switch. An LTVS16H5.0 TVS sits on that 5 V rail.
- So **no source Rp (56 kΩ) is needed**: the board takes VBUS regardless of CC. A plain 2-wire USB-C power pigtail (VBUS + GND) works. That cable isn't strictly USB-C-compliant (no Rp), which doesn't matter for this fixed connection.
- If you prefer a compliant source, use an Adafruit 5978 plug breakout with its 5.1k CC1 jumper cut, and a 56 kΩ from VBUS to CC1. That's optional.
- Cut the pigtail short (20–30 cm). At 22 AWG the drop is under 0.1 V at 0.7 A.
- Don't solder to on-board 5 V pads; the USB socket is the clean path.
- The "UART" Type-C socket feeds 5 V through its own P-FET. Leave it and the battery header empty.

## Parts

Full details, specs and links are in [bom.csv](assets/n2k-power/bom.csv).

| Ref | Qty | Part | Part number | Notes |
|-----|-----|------|-------------|-------|
| U1 | 1 | 5 V 3.2 A step-down regulator | [Pololu D36V28F5](https://www.pololu.com/product/3782) (#3782) | 5.3–50 V in, reverse protection to 40 V. |
| F1a | 1 | Waterproof inline ATO/ATC fuse holder | [Blue Sea 5065](https://www.bluesea.com/products/5065/Waterproof_In-Line_ATO_ATC_Fuse_holder) | Fuse sold separately. The 5064 is the non-waterproof version. |
| F1b | 1 + spares | 1 A ATO blade fuse, fast-acting | Littelfuse 0287001.PXCN | Don't fit a lower rating. |
| J2 | 1 | USB-C male to 2 bare wires, power only | Generic 22 AWG pigtail (e.g. XMSJSIY LE-103) | Into the socket marked **USB**. Check polarity with a meter first. |
| D1 | 0–1 | Bidirectional TVS, optional | Littelfuse P6KE27CA | Soldered across Pololu VIN/GND. |
| C1 | 0–1 | 100 µF 50 V electrolytic, optional | Panasonic EEU-FC1H101 | Only if the hot-plug test shows VIN over about 35 V. |
| — | as needed | Adhesive-lined marine splices | generic | 12 AWG fuse pigtail to drop wire. |
| — | 1 | Small IP66/67 box, two cable glands | generic | For the Pololu and splices. See below. |

## Wiring and enclosure

1. NET-S (red) → fuse holder → Pololu VIN. NET-C (black) → Pololu GND.
2. Pololu VOUT → pigtail red. Pololu GND (output side) → pigtail black. Leave EN and PG open (EN is pulled up on board).
3. CAN-H (white) / CAN-L (blue) → Waveshare CAN header H / L, kept twisted. Remove jumper 13 (120 Ω).
4. Shield/drain: cut back and insulate. It isn't connected; N2K grounds it only at the power tap.
5. **Single ground:** NET-C = Pololu GND = USB-C GND = board GND = the TJA1051's reference. While the board is on the bus, nothing else is wired to it: no laptop USB, no RS485, no second 12 V.
6. Enclosure:
   - **Box:** a small IP66/67 box with two cable glands. Mount the Pololu on M2 standoffs.
   - **Fuse holder:** put it outside the box, or inside if there's room, so the fuse stays reachable.
   - **Coating:** conformal-coat the Pololu, but not the pins you solder to afterwards.
   - **Mounting and strain relief:** strain-relieve the drop and the pigtail, and mount the box in a dry spot. It dissipates under 0.4 W, so heat isn't an issue.
   - **Display case:** a 3D-printable [case for the Waveshare ESP32-S3-Touch-LCD-7](https://www.printables.com/model/1558735-waveshare-esp32-s3-touch-lcd-7-case) is on Printables. It covers the display board only. Openings for the N2K drop / CAN entry and the USB-C lead are drilled after printing and sealed with Sikaflex-291. Whether the Pololu, the fuse and the drop splice fit inside it hasn't been checked, so plan on the separate small box. See [19](19-n2k-backbone-install.md#2-parts-list).

## Bench test

Use a bench supply with a current limit, and a scope. Do this before the board goes near the boat. It comes before the bus checks in [19](19-n2k-backbone-install.md#5-bench-test-before-the-boat), section 5.

1. **No-load:** at 9, 12 and 16 V, VOUT should be 4.8–5.2 V. Input current should be a few mA.
2. **Load steps:** use an electronic load on the pigtail. At each of 9 / 12 / 16 V, step 0 → 0.5 → 1.0 A and back. Check VOUT stays within 4.75–5.25 V and record input current. At 9 V and 0.7 A, input should be ≤ 0.46 A.
3. **Real load:** with the Waveshare at full backlight and Wi-Fi transmitting (`NMEA_SOURCE` set), measure input current at 9 V and 12 V. Update the LEN figure in [19](19-n2k-backbone-install.md#3-len--power-budget) from this.
4. **Hot-plug:** feed the PSU through about 6 m of drop cable (or the real Micro-C drop through a tee) at 16 V. Scope VIN at the Pololu pins (10× probe, short ground spring) and input current (current probe or a 0.1 Ω shunt). Plug in 10 times.
   - Pass: VIN peak under 35 V, with no Pololu reset loop.
   - If it exceeds 35 V: fit C1 (and/or D1) and repeat.
   - Record the peak inrush current and its duration.
5. **Low-voltage ride-through:** sweep the input down from 9 V to 6 V and check where 5 V drops out (expect about 6 V at 0.7 A). Dip briefly to 8 V and confirm the board doesn't reboot.
6. **Reverse polarity:** apply −16 V (PSU current-limited to 1 A) for 10 s. Expect about 0 A, no output, fuse intact. Then restore +12 V and confirm normal operation.
7. **TVS check (if D1 fitted):** at 16 V, leakage should be in the µA range. Slowly raise the PSU, current-limited to 20 mA, to about 26–28 V and confirm it starts conducting (Vbr 25.7–28.4 V). Don't go further.
8. **Fuse:** short VOUT briefly at 12 V. The Pololu should hiccup or current-limit and recover, and the fuse should not blow. A hard short on VIN should blow the fuse.
9. **Heat:** run 1 h at 9 V and 0.7 A inside the closed enclosure. The module should be under 70 °C.

## Risks and open points

- **No transient spec:** Pololu publishes no separate transient or abs-max beyond "up to 50 V". Our margin rests on the TVS (if fitted) and on the bus being a suppressed 12 V system.
- **Interpolated efficiency:** the 9 V and 16 V efficiencies are read from a typical graph, not guaranteed. The worst-case 87 % figure still fits 10 LEN, but only just (0.46 A). Measure it.
- **Estimated load:** the board load (0.7 A peak) is an estimate. If the measurement exceeds about 0.75 A at 5 V, the 9 V bus current passes 0.5 A and the LEN must go to 11–12.
- **Drop resistance:** it was assumed (22 AWG, 6 m). A shorter drop helps.
- **Fast fuse:** the ATOF fuse is fast-acting, not slow-blow. That's fine for this inrush, but don't swap in a lower rating.
- **Third-party pigtail:** the USB-C pigtail is a generic third-party part. Inspect the polarity (red = VBUS) with a meter before first plug-in.
- **Not certified:** this is a home-made bus-powered node. It's not N2K-certified, so club acceptance rests on the bench results.

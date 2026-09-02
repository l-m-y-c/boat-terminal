# 17 – Android Boat / BoatLink Roadmap

**Status:** bench pairing works (QR + BLE OOB). This document is the build plan for the next layer.
**Audience:** humans and Grok Build. Prefer this file over chat history when implementing.
**Related:** [03 architecture](03-system-architecture.md), [05 NMEA](05-nmea-connectivity.md), [06 pairing](06-phone-app-and-pairing.md), [16 Tritium](16-related-projects-tritium.md).

---

## One-sentence product

The phone is the computer. The Waveshare is the boat’s I/O hub: it aggregates NMEA, shows glanceable instruments, and (after pairing) opens a Wi-Fi data plane to the member’s apps. It is not a tiny Android tablet and it is not a chartplotter.

## Current repo state (2026-09-02)

| Piece | What exists | What does not |
|-------|-------------|----------------|
| Hardware | Waveshare ESP32-S3-Touch-LCD-7, 800×480 RGB + GT911, CAN + RS485 headers, TF slot, Wi-Fi + BLE 5 | Galvanic isolation, 12 V path, enclosure |
| Firmware | LVGL v8 ready/QR UI, NimBLE peripheral, OOB pairing, Wi-Fi **STA only**, SD FAT32, diagnostics card | NMEA parser, SoftAP, TCP/UDP gateway, instrument pages, JPEG ingest |
| BLE GATT | Service `6c6d7963-0001-4000-8000-000000000001`. Payload read `…000010`, session WRITE `…000020` (raw 16-byte OOB), status READ/NOTIFY `…000021` | NMEA notify, inject write, control plane beyond PING |
| Mobile | Flutter Android, `lmyc://` deep link, Find terminals, raw16 OOB write | Sentence console, Wi-Fi client to gateway, GPS formatter |
| Safety docs | Isolation + “instruments survive pairing expiry” are hard requirements | No live bus code yet — keep it that way until Slice C is gated |

Known constraints the next code must respect:

- Default ATT MTU is 23 (20-byte payload). Firmware advertises preferred MTU 185 but must not require it. Never send a 37-byte `PAIR <hex>` write.
- Session characteristic `…000020` is write-only on purpose. Do not reuse it for status or NMEA. Android read-after-write on that char is garbage.
- NimBLE 1.4 `ble_gap_conn_desc` has no `att_mtu`. Use `getPeerMTU()` / `onMTUChange`.
- Wi-Fi reconnect already uses backoff so it does not starve BLE advertising. Keep that.
- Display is 800×480 RGB565 (~768 KB/frame). ESP32-S3 has no H.264 decoder. Live app-mirroring is out of scope (see Non-goals).

---

## Design principles (do not violate)

1. **Phone = powerhouse.** Charts, booking, photos, derived numbers, updates live on the phone.
2. **Terminal = aggregator + cluster.** Local NMEA display works with the phone in a pocket or with pairing expired.
3. **BLE = control plane. Wi-Fi = data plane.** Pairing, page select, inject enable, compact state on BLE. Sentence firehose and images on Wi-Fi.
4. **Listen first, talk later.** v1 reads the boat bus. Writing onto a live backbone is an explicit, gated mode.
5. **Professional tools stay primary.** No autopilot commands. No interactive chartplotter on the 7″ or the E-Ink.
6. **Club first.** Logbook, maintenance, handover, and a sentence recording tied to a booking matter more than a projector demo.
7. **Open and small.** One volunteer must be able to flash and explain it. Prefer boring protocols (NMEA 0183 text, TCP 10110, JPEG).

## Non-goals for this roadmap

- Full-motion screen mirroring of Navionics (or any third-party app) onto the 7″.
- Pinch-zoom remote desktop / scrcpy / Miracast / Android Auto projection protocol.
- Signal K server on the ESP32 (revisit only if the fleet grows a Pi-class boat computer).
- Autopilot control, route planning, legal-logbook replacement.
- Injecting phone GNSS onto NMEA 2000 in the first slices.
- Meshtastic as a core path.

Allowed later, not now: on-demand JPEG *snapshots* (phone-composed 800×480 frames) to the 7″ and the 13.3″ E-Ink. Same idea as doc 03 “image push,” not a live cast.

---

## Target architecture

```
                    member phone
         ┌──────────────────────────────┐
         │ LMYC app          Navionics / OpenCPN │
         │  BLE session         TCP/UDP 10110    │
         │  sentence console    (Wi-Fi NMEA)     │
         │  optional phone GNSS formatter        │
         └──────────────────────────────┘
                    │ BLE                 │ Wi-Fi
                    │ control             │ data
                    │                     │
         ┌──────────────────────────────┐
         │         Waveshare 7" terminal         │
         │  pairing + BoatLink session           │
         │  NMEA parser + LVGL instrument tiles  │
         │  TCP/UDP gateway + optional SoftAP    │
         │  SD ring log                          │
         └──────────────────────────────┘
                    │ listen (v1)
         boat 0183 RS485 / later isolated N2K
```

### Transport rules

| Traffic | Path | Why |
|---------|------|-----|
| QR identity + OOB secret | On-screen QR `lmyc://pair?...` | Already works |
| Pair / unpair / PING | BLE session `…000020` + status `…000021` | Already works |
| Compact instrument state (depth, SOG, …) | BLE notify, small ASCII or packed | Survives without Wi-Fi |
| Raw NMEA 0183 lines | Wi-Fi TCP 10110 + UDP 10110 (and/or 2000) | What Navionics expects |
| Debug tap of the same lines | BLE notify `…000030` *or* TCP, rate-limited | Phone console while still on BLE |
| Phone GNSS inject | Wi-Fi TCP write or BLE `…000031`, **gated** | Must not be a silent bus talker |
| JPEG snapshots (later) | HTTP POST or raw TCP | Too big for BLE |

### Networking modes (add in this order)

1. **STA to a known SSID** — already implemented via `secrets.h`. Keep for bench.
2. **STA to the member’s phone hotspot** — same code, different SSID provisioned after pair. Zero extra radio mode.
3. **SoftAP `LMYC-<boat>`** — Slice B. Phone and Navionics join the *boat*. This is how commercial Wi-Fi multiplexers work.

Do not take down BLE advertising while flipping Wi-Fi modes. Coex + reconnect backoff already exist; extend them.

---

## GATT additions (do not overload existing chars)

Existing (leave behaviour unchanged):

| UUID | Role |
|------|------|
| `6c6d7963-0001-4000-8000-000000000001` | Primary service |
| `…000010` | Payload URI, READ |
| `…000020` | Session WRITE / WRITE_NR (raw16 OOB, `PING`) |
| `…000021` | Status READ / NOTIFY (`WAIT` / `OK` / `FAIL` / `PONG`) |

Add:

| UUID | Props | Payload |
|------|-------|---------|
| `6c6d7963-0001-4000-8000-000000000030` | NOTIFY + READ | One NMEA 0183 sentence per notification, including `$`/`!` and `*xx`. No NUL. If MTU is 23, drop sentences longer than 20 bytes on BLE (they still go out Wi-Fi). |
| `6c6d7963-0001-4000-8000-000000000031` | WRITE / WRITE_NR | Inject. Accept a full sentence *or* `INJECT ON` / `INJECT OFF` / `INJECT LOCAL`. Reject bus-directed inject unless the terminal gate is armed. |
| `6c6d7963-0001-4000-8000-000000000032` | NOTIFY + READ | Compact state, ASCII: `dpt=12.4,sog=5.1,cog=214,aws=11.0,awa=-22,fix=boat` |
| `6c6d7963-0001-4000-8000-000000000033` | WRITE / WRITE_NR | Control: `PAGE inst`, `GW ON`, `GW OFF`, `REC ON`, `REC OFF` |

Firmware version string on DIS `2A26` should bump when these chars appear (`boatlink-v1`).

---

## NMEA rules for v1

- Speak **0183 text** on the phone and Wi-Fi side even if the source is later N2K. Convert on the terminal.
- Parser: line-buffered, 82-char cap (plus CR/LF), XOR checksum, count good/bad/unknown.
- First sentences to decode into tiles / compact state: `RMC`, `GGA`, `VTG`, `GLL`, `DBT`/`DPT`, `MWV`, `MWD`, `VHW`, `HDT`/`HDM`/`HDG`, `RSA`, `XDR` if obvious.
- Pass **unknown valid-checksum sentences** through the gateway untouched. Do not drop AIS `!AIVDM` just because we do not decode it yet.
- Baud: default **38400**, configurable 4800/9600/19200/38400. Waveshare RS485 header is the physical port; until the boat is wired, accept input from a second UART *or* a built-in simulator.
- Simulator (required for Slice A, no hardware transducer): firmware can emit a canned loop of RMC/GGA/DBT/MWV when `SIM ON` or when no bytes have arrived for 3 s *and* sim mode is enabled. Label the UI `FIX: SIM` so nobody thinks it is the keel.
- Recording: optional ring on SD, one file per session, FAT32. Booking id later; `BENCH-01` for now.

### Inject policy (Slice C — do not implement a bus talker before this table exists in firmware)

| Mode | Where phone GNSS goes | Default |
|------|----------------------|---------|
| `OFF` | Dropped | Yes |
| `LOCAL` | Parser + 7″ tiles + Wi-Fi gateway back to phones. **Not** RS485/CAN TX | Bench / first boat install |
| `BACKUP` | Same as LOCAL, plus isolated 0183 TX, only if no boat RMC for N seconds | Off until a human arms it on the 7″ |
| `BUS` | Forbidden in v1 | — |

Phone GNSS may emit `RMC`/`GGA`/`VTG`/`GLL` only. Never invent depth, wind, or heading.

On-screen chip whenever the fix source is not the boat: yellow `FIX: PHONE` or `FIX: SIM`.

---

## Slices

Ship in order. Each slice is a PR-sized unit with a demo you can do on the bench without a boat.

### Slice A — NMEA tap + console + simulator  **← Grok Build starts here**

**Goal:** prove sentences move terminal ↔ phone. No boat wire required.

Firmware:

- New files (suggested):
  - `firmware/src/nmea.h` / `nmea.cpp` — line assembler, checksum, talker/formatter split, counters.
  - `firmware/src/nmea_sim.h` / `nmea_sim.cpp` — 1 Hz canned Vancouver-ish position + depth + wind.
  - `firmware/src/nmea_io.h` / `nmea_io.cpp` — UART/RS485 reader. If RS485 pins are still being confirmed, compile-time fallback to USB-CDC RX or an unused UART so the sim path is not blocked.
  - `firmware/src/gateway.h` / `gateway.cpp` — TCP server on port **10110**, one client is enough for A. Echo every good line + sim line with CRLF.
- Extend `pairing.cpp` with char `…000030` notify (rate-limit to ~10 Hz on BLE).
- UI: diagnostics card gains `NMEA good/bad/s` and `TCP 10110 <clients>`. Do not redesign the whole screen.
- Keep pairing behaviour identical. `make pair-test` must still pair.

Mobile:

- New `mobile/lib/nmea_console.dart` (or split if the file would exceed ~400 lines).
- After successful pair, a **Sentences** screen:
  - live list (newest at top), pause, clear, copy-all
  - filter box (`RMC`, `DPT`, `!AI`)
  - counters: received / bad checksum / rate
  - subscribe to `…000030`; if notify is empty, also try TCP `terminal-ip:10110` when Wi-Fi IP is already shown on the terminal
- Do not add GPS inject yet.

Acceptance:

1. Terminal boots, advertises, pairs with the existing app flow.
2. With sim on, 7″ diagnostics show a rising good-sentence count.
3. Phone Sentences screen shows `$GPRMC` / `$GPDPT` / `$WIMWV` (or equivalent talkers) updating.
4. From a laptop on the same STA network: `nc <terminal-ip> 10110` prints the same stream.
5. Firmware CI (`pio run` in `.github/workflows/firmware.yml`) stays green.
6. No change to the raw16 OOB write path.

Demo script for humans:

```text
make flash && make monitor     # confirm "NMEA sim on" in serial
make phone                     # pair as today
open Sentences
# optional: nc $TERMINAL_IP 10110
```

### Slice B — Wi-Fi NMEA gateway + SoftAP

**Goal:** Navionics / OpenCPN on the phone consume boat (or sim) data the standard way.

- SoftAP `LMYC-<boat id>` with password from `secrets.h` (`AP_PASSWORD`). STA remains available.
- UDP broadcast 10110 (and 2000 if cheap) in addition to TCP 10110.
- Multiple TCP clients (at least 2: LMYC app + Navionics).
- UI shows mode `STA ip` or `AP 192.168.4.1`.
- App: “Gateway” card with IP, port, copy, and a one-screen “how to add this in Navionics” note.
- Still no bus TX.

Acceptance: phone joins the terminal AP, Navionics “Add device” TCP `192.168.4.1:10110` shows speed/course from sim. LMYC console still works.

### Slice C — gated phone GNSS + first instrument tiles

**Goal:** phone can feed a LOCAL fix; 7″ shows big digits from parsed state.

- App formats `RMC`/`GGA`/`VTG` at 1 Hz from Android location (use the platform NMEA callback when present; otherwise format from `Location`).
- Terminal accepts inject only in `LOCAL` (default off). Arming is a 7″ button, not a phone-only flag.
- LVGL page: depth, SOG, COG, AWS/AWA, fix source chip.
- Compact state char `…000032` so the app can show a strip without parsing.
- Isolated RS485 TX is **not** wired in this slice. No `BACKUP` bus write.

Acceptance: kitchen-table demo. Walk around the block. 7″ SOG/COG tracks the phone. Yellow `FIX: PHONE`. Killing inject returns to sim or silence. Pairing expiry does not blank the tiles.

### Slice D — real 0183 listen + SD recording

**Goal:** a USB-RS485 or the onboard RS485 header talks to a bench GPS / multiplexer.

- Confirm Waveshare RS485 pins in `nmea_io.cpp` against the wiki; document in `firmware/README.md`.
- SD ring: `/nmea/YYYYMMDD-HHMMSS.log`, rotate at 2 MB.
- Diagnostics: last talkers seen, baud.
- Still listen-only on the wire.

### Slice E — snapshots, not mirroring

**Goal:** phone-composed JPEG → 7″ (and later E-Ink).

- 800×480 JPEG, 1 frame on demand, decode with Espressif JPEG, blit under LVGL.
- Use cases: checklist card, open-defect photo, static chart excerpt.
- Hard cap: ≤ 1 fps. No MediaProjection pipeline.

### Slice F — isolated N2K listen (pilot boat only)

Out of band with Board / insurance. Separate hardware note. Use `NMEA2000` library family. Convert a useful PGN set to 0183 so Slices A–E stay unchanged. No TX.

---

## File ownership (do not dump everything in main.cpp)

| Path | Owns |
|------|------|
| `firmware/src/main.cpp` | Bring-up, UI shell, timers. Keep thin. |
| `firmware/src/pairing.*` | BLE identity, OOB, GATT registry. New chars live here *or* a new `boatlink.cpp` that pairing starts. |
| `firmware/src/nmea.*` | Parse / checksum / state. |
| `firmware/src/nmea_io.*` | Bytes in/out of UART/RS485. |
| `firmware/src/nmea_sim.*` | Fake talker. |
| `firmware/src/gateway.*` | TCP/UDP. |
| `mobile/lib/main.dart` | Pairing UX. Extract BLE + NMEA into `mobile/lib/ble/` and `mobile/lib/nmea/` if main grows. |

CI: `.github/workflows/firmware.yml` must still `pio run`. Do not add a secrets.h to git; if compile needs `WIFI_SSID`, keep using the example path already in tree.

---

## What Grok Build should do in the first session

Implement **Slice A only**. Do not SoftAP, do not GNSS inject, do not JPEG, do not redesign pairing, do not introduce Signal K.

Suggested first-session checklist:

1. Read this file and `firmware/src/pairing.h`, `pairing.cpp`, `main.cpp`, `mobile/lib/main.dart`.
2. Add the NMEA parser + simulator + TCP 10110 server + BLE `…000030` notify.
3. Add the Flutter Sentences screen behind the existing paired state.
4. Leave a short `docs/17` note in the PR/commit body listing what was verified on-device vs compile-only.
5. Stop.

If RS485 pinout is uncertain, ship with **sim-only input** plus TCP output so the phone work is unblocked. Pin the RS485 question in a `// TODO(slice-d)` comment rather than guessing a boat-facing TX pin.

---

## Open hardware questions (do not block Slice A)

- Exact RS485 UART + DE/RE pins on the Waveshare ESP32-S3-Touch-LCD-7 (wiki vs our `esp_panel_board_custom_conf.h`).
- Whether SoftAP + STA APSTA is stable with NimBLE on this module (Slice B risk).
- Isolated 0183 transceiver choice for the pilot boat (Slice D/F, Board).

---

## Success for the club, not just the bench

A member arrives, pairs, and their **existing** Navionics session sees boat depth and wind. The 7″ shows the same numbers if they pocket the phone. A volunteer can open Sentences and see whether the transducer is actually talking. Nobody injected a second GPS onto the autopilot by accident.

That is Android Boat.

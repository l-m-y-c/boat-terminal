# Baca Lele: the Garmin GPSMAP 740s profile

What the bus on Baca Lele looks like, what the terminal does with it, how the
chart apps must be set, and the checklist for the next visit. Background:
[boat-test-2026-10-28.md](boat-test-2026-10-28.md).

## The bus

| Address (typical) | Device | Role |
|---|---|---|
| 1 (dynamic) | Garmin GPSMAP 740s chartplotter, internal 10 Hz GPS | the only data source |
| 22 (claimed, may move) | LMYC boat-terminal (maker 2046, product 666) | listens, converts, publishes its own identity |

No heading sensor, depth, wind or AIS on this boat. Everything chart apps get
comes from the 740s internal GPS, **provided the 740s is set to send it on
NMEA 2000** (Settings > Communications > NMEA 2000 Setup; on some firmware the
internal GPS is only output if "Position source" is the internal receiver).

## What the 740s should send

| PGN | Name | Rate | We use it for |
|---|---|---|---|
| 129025 | Position, rapid update | 10 Hz | RMC/GGA/GLL lat/lon |
| 129026 | COG & SOG, rapid | ~4 Hz | RMC/VTG COG/SOG (true; magnetic is converted with 127258) |
| 129029 | GNSS position data | 1 Hz | time, date, fix quality, sats, HDOP, altitude (GGA). Optional: without it RMC still comes out |
| 126992 | System time | 1 Hz | date/time fallback |
| 127258 | Magnetic variation | 1 Hz | RMC variation, magnetic COG |
| 129539 | GNSS DOPs | 1 Hz | not forwarded |
| 126993/126996/126998/60928/59904 | network management | | device inventory (`NMEA NET`) |

If a capture shows only the network management PGNs from the 740s, it is not
transmitting GPS on the backbone: fix it on the 740s, not in our code.

## What the terminal produces

From the above, at 1 Hz: `$GPRMC`, `$GPGGA`, `$GPGLL`, `$GPVTG` (checksummed),
on TCP 10110 (and UDP broadcast) on its hotspot. The phone relay re-serves it
on the phone at `127.0.0.1:10110`. The host test
`firmware/test_host/run.sh` replays a synthetic 740s stream
(`tools/garmin740s_capture.py`) and checks exactly this.

The terminal announces itself as model ID **LMYC boat-terminal**, installation
description 1 = boat name (**Baca Lele**), description 2 = "LMYC boat-terminal
<id>". The strings are plain ASCII (host test checks the 126996 bytes). If the
740s still shows a garbled name, it is the Garmin's rendering of an
unregistered maker code (2046) or a stale cached entry: power-cycle the 740s
and check again, and look at our own TX frames in the bus ring.

## Chart app settings (phone on the relay)

| App | Setting |
|---|---|
| Navionics Boating | Menu > Paired devices > add: **TCP**, host `127.0.0.1`, port `10110` (or UDP 10110 / 2000). GPS source must be the external device, not the phone. |
| iBoating | Settings > NMEA / Instruments > TCP `127.0.0.1:10110`, enable "Use NMEA GPS". |

The Join tick in the LMYC app must be green (relay running) before the chart app connects.

## Next visit checklist

1. Laptop on battery only. Terminal on the backbone, phone joined (green tick).
2. On the 740s: NMEA 2000 setup, confirm GPS output is on; Device list shows "LMYC boat-terminal / Baca Lele".
3. Chart app: settings above; motor/move a few metres; COG/SOG should appear.
4. Before powering anything off: `make boat-diag` (UART to the terminal, laptop on the
   hotspot, phone on USB with debugging allowed). Rings live in PSRAM and are lost on power-off.
5. In the pull: `analysis/summary.txt` (which PGNs the 740s sent), `wifi/nmea-ring.txt`
   (sentences we produced, TCP client connects), `phone/files/relay-log.txt`
   (Wi-Fi request/lost/unavailable, chart app connects, sentence counts per 30 s).

## Getting debug data off a closed enclosure: the phone

Once the enclosure is closed the USB-C ports are out of reach, so the phone
app is the conduit. **Diagnostics tab > Collect diagnostics** pulls, over the
boat Wi-Fi (through the relay on 127.0.0.1:10180):

| In the zip | From |
|---|---|
| `terminal/bus-ring.log` | `/n2k/frames?after=1`, the whole 32768-frame ring incl. our own TX, candump format (`capture.py summary/replay` reads it) |
| `terminal/nmea-ring.txt` | `/diag/nmea.txt`: sentences produced (S), bus events (E), log lines (L) |
| `terminal/status.json` | boat, firmware version (`fw`), reset reason, uptime, TCP clients, NMEA counters, stats |
| `terminal/n2k.json` | bus state, errors, PGN table, converter counts |
| `terminal/network.json` | device inventory: the 740s NAME, product info (model, software, serial), our own entry |
| `phone/relay-log*.txt` | the relay diary: Wi-Fi requests and callbacks, chart apps connecting and leaving, sentence counts per 30 s |
| `phone/wifi-status.json`, `phone/ble-stats.json`, `phone/report.txt` | Wi-Fi/relay state, the terminal's Bluetooth stats, the Diagnostics report with recent app events |
| `manifest.json` | when, why (manual / problem / auto-bus-off / auto-bus-errors / auto-no-frames), the member's note, app version, chart-app stats |

Saved as `lmyc-diag-YYYYMMDD-HHMMSS-<reason>.zip` in
`/sdcard/Android/data/ca.lmyc.boat/files/diagnostics/` (last 30 kept),
each with a Share button (email, Drive, ...). If the terminal does not answer
the zip still holds the phone side and the card says so.

- **Mark problem**: same, with a note, at the moment something looks wrong.
- **Automatic snapshot**: while paired on NMEA 2000 the app watches the 2 s
  Bluetooth stats and collects by itself on BUS-OFF/failed, a jump of 200+ bus
  errors, or frames stopping for 20 s after they had flowed (each reason at
  most once per 15 min). The terminal's rings are RAM-only, so this keeps the
  evidence even if it reboots later.
- **On the bench**: `make boat-diag` (or `python3 tools/boat_diag.py --no-uart --no-wifi`
  with only the phone on USB) pulls the folder and unpacks every zip into
  `sessions/<now>/phone/bundles/<name>/`; the analysis runs on the newest
  bundle's bus ring when there is no Wi-Fi pull. `make boat-diag VIA_PHONE=1`
  also reaches the terminal live through the phone's relay (adb forward of
  10180/10110), so the Mac never has to join the hotspot.

Persisting a snapshot on the terminal itself is not done: the ring is 1 MB of
PSRAM and the boat build has no filesystem partition for it; the phone's
automatic snapshot covers the reboot case. `status.json` now carries the
reset reason (`reset`, esp_reset_reason) so a bundle shows whether the terminal
rebooted from a brown-out (9), panic (4) or watchdog (5-7).

# Developer tools

Tools for building, flashing and debugging the boat terminal, at the bench and on the boat. They are plain Python 3.11+ with no packages to install, and run the same on the Mac and on Linux / WSL. The Makefile runs them with uv's Python when uv is installed.

| Tool | For |
|------|-----|
| `tools/board.py` | Find the board, see what it is running, get it out of the bootloader, flash it and check it booted |
| `tools/console.py` | The serial console: log, commands, `NMEA DIAG` snapshots, and the boat session |
| `tools/terminal.py` | The terminal over its hotspot: status, bus check, recording the bus, the 0183 stream chart apps get |
| `tools/capture.py` | Look inside a bus recording, and replay it through the terminal's real code on the host |
| `tools/n2k_play.py` | Send one console command, `NMEA DEMO` by default (older, kept for `make n2k-demo`) |
| `tools/check_protocol.py` | CI: the firmware, app and stand-in declare the same UUIDs and limits |
| `tools/maint_card_server.py` | Stand-in for the terminal's maintenance web server |

## The two Type-C sockets

| Socket | Chip | Use it for |
|--------|------|------------|
| **UART** | CH343 USB-serial (VID `1a86`) | Logs and commands, in **every** mode. Opening it never resets the board. The only log port on NMEA 2000. |
| **USB** | ESP32-S3 USB Serial/JTAG (VID `303a`) | Flashing. Exists **only on the simulator**: on NMEA 2000 the board moves GPIO19/20 to the CAN transceiver and this socket goes away by design. |

The serial tools pick the UART socket when it is there and never toggle DTR/RTS, so they don't reset the board. Pass `--usb` to use the USB socket, or `--port /dev/…` to choose one.

## Make targets

```text
make board-ports        serial ports, and which socket is which
make board-status       app / bootloader / silent / absent
make board-attach       WSL: attach the board with usbipd --auto-attach
make board-boot         ROM bootloader -> app (USB socket)
make flash              build, upload, check the app booted, recover if not
make flash-pio          plain PlatformIO upload

make console            stream the log (also saved to console-<date>.log)
make send CMD="NMEA STATS"
make diag               NMEA DIAG, summarised
make watch              a status line every 10 s
make session            boat mode, see below

make bus-check          GET /n2k.json over the boat Wi-Fi, summarised
make bus-status         GET /status.json
make record SECONDS=120 OUT=dock.log
make nmea-tcp           30 s of TCP 10110: sentence rates, bad checksums

make capture-summary CAP=dock.log [TERMINAL=23]
make replay CAP=dock.log      -> dock.log.0183.txt and dock.log.n2k.json
make sim-capture              a 60 s capture from the simulator

make tools-test         the tools' own tests (also part of make test)
```

## On the boat

Follow [docs/21](../docs/21-first-boat-run.md). The tools fit in like this.

**Isolation first.** The prototype's CAN interface is not isolated. A laptop on the UART cable must run **on its own battery**, not charging, with nothing else plugged into it. Never use a mains-powered computer while the terminal is on the backbone.

1. Plug the laptop into the terminal's **UART** socket and run `make board-status`. It should say `app`.
2. `make session` streams the whole log into `sessions/<date-time>/console.log` and saves an `NMEA DIAG` snapshot every 10 s to `diag.jsonl` and `diag-latest.json`. On screen you get one status line per snapshot, the `N2K:` log lines (address claimed, bus-off and so on), and the hint text whenever it changes. Ctrl-C prints a full summary of the last snapshot.
3. Recordings come from the phone (**Bus check → Record bus → Share recording**, AirDrop to the Mac). From a laptop on the boat Wi-Fi, use `make record SECONDS=120 OUT=…` instead. Note that the laptop then has no internet.
4. Look at a recording straight away with `make capture-summary CAP=… TERMINAL=<address>`. It lists:
   - every device and what it claims to be;
   - each PGN's rate, and fast-packet completion;
   - repeated frames, which point to a wiring or termination fault;
   - whether the terminal sent anything except network management.
5. Back at the bench, `make replay CAP=…` runs the recording through the real NMEA2000 library, the diagnostics and the 0183 converter. That shows exactly what the terminal made of it, and the capture can become a test.

To flash on the boat, **unplug the terminal's drop from the backbone first**, switch it to **Simulator** on the Data source screen, then run `make flash` on the USB socket. Plug the drop back in afterwards.

## Board trouble, and what the tools do about it

| Symptom | Cause | Tool |
|---------|-------|------|
| USB port gone on WSL after a reset | usbipd drops a device that re-enumerates unless it was attached with `--auto-attach` | `make board-attach` |
| Console silent after a flash | The upload's reset left the chip in the ROM bootloader | `make board-status` says `bootloader`; `make board-boot` starts the app. `make flash` does this itself. |
| No USB socket at all | The terminal is on NMEA 2000 | Use the UART socket, or switch to Simulator to flash |
| Windows never sees a device | Cable, or company device control (`board-status` reports CrowdStrike's `CsDeviceControl` filter when usbipd sees it) | Different cable or socket. For policy, ask IT; don't work around it. |
| Opening a port resets the board | Some serial programs toggle DTR/RTS | Use these tools, which leave the lines alone |

## Files

Recordings are candump log lines, the format canboat's `analyzer` reads:

```text
(12.345678) can0 09F80123#0102030405060708
```

The timestamp is seconds since the terminal started. Lines starting `#` are notes: start, stop, frames lost. `NMEA DIAG` and `GET /n2k.json` give the same JSON (devices, per-PGN counters, converter counters, which device each value comes from, hint), described in [docs/05](../docs/05-nmea-connectivity.md#diagnostics).

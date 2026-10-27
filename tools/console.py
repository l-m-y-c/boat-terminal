#!/usr/bin/env python3
"""The terminal's serial console: log, commands, NMEA DIAG snapshots.

    python3 tools/console.py log                 # stream the log (Ctrl-C stops)
    python3 tools/console.py log --tee boat.log  # and keep a copy
    python3 tools/console.py send NMEA STATS     # one command, print the reply
    python3 tools/console.py diag                # NMEA DIAG, summarised
    python3 tools/console.py diag --out diag.json
    python3 tools/console.py watch --every 10 --out session/   # snapshots only
    python3 tools/console.py session              # boat mode: log + snapshots, saved

Uses the UART socket (CH343) when it is there: it works on NMEA 2000 and
opening it never resets the chip. --usb picks the USB socket instead.
Console commands: NMEA SIM | N2K | STATS | DIAG | DEMO | PGN <n> [src=N] <hex>.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import sys
import time
from pathlib import Path

from lmyc_dev.diag import find_json, summarise_n2k
from lmyc_dev.ports import Console, find_port


def _stamp() -> str:
    return dt.datetime.now().strftime("%H:%M:%S")


def _open(args: argparse.Namespace) -> Console:
    port = find_port("usb" if args.usb else "uart", args.port)
    print(f"# {port.describe()}", file=sys.stderr)
    return Console(port.path)


def cmd_log(args: argparse.Namespace) -> int:
    tee = open(args.tee, "a", encoding="utf-8") if args.tee else None
    try:
        with _open(args) as con:
            for line in con.lines(args.seconds):
                text = f"{_stamp()} {line}"
                print(text, flush=True)
                if tee:
                    tee.write(text + "\n")
                    tee.flush()
    except KeyboardInterrupt:
        pass
    except ConnectionError as exc:
        print(f"# {exc} (chip reset or cable out)", file=sys.stderr)
        return 1
    finally:
        if tee:
            tee.close()
    return 0


def cmd_send(args: argparse.Namespace) -> int:
    line = " ".join(args.words)
    with _open(args) as con:
        for reply in con.command(line, args.seconds):
            print(reply)
    return 0


def _diag(con: Console) -> tuple[dict | None, list[str]]:
    lines = con.command("NMEA DIAG", 3.0)
    return find_json(lines), [l for l in lines if l.startswith("NMEA ")]


def cmd_diag(args: argparse.Namespace) -> int:
    with _open(args) as con:
        data, heads = _diag(con)
    for h in heads:
        print(h)
    if data is None:
        print("No diagnostics JSON in the reply (older firmware, or the console is busy).")
        return 1
    if args.raw:
        print(json.dumps(data, indent=2))
    else:
        print(summarise_n2k(data))
    if args.out:
        Path(args.out).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        print(f"\nSaved {args.out}")
    return 0


def cmd_watch(args: argparse.Namespace) -> int:
    """Every N seconds: a one-line summary, the hint when it changes, and
    each snapshot appended to <out>/diag.jsonl for later."""
    out = Path(args.out) if args.out else None
    if out:
        out.mkdir(parents=True, exist_ok=True)
    last_hint = None
    with _open(args) as con:
        try:
            while True:
                data, _ = _diag(con)
                if data is None:
                    print(f"{_stamp()} no diagnostics")
                else:
                    bus, me = data.get("bus", {}), data.get("self", {})
                    lost = bus.get("lost_q", 0) + bus.get("lost_drv", 0) + bus.get("lost_hw", 0)
                    print(f"{_stamp()} {bus.get('state')} {bus.get('fps', 0)} f/s, "
                          f"{len(data.get('devices', []))} devices, node {me.get('node')} "
                          f"{me.get('address', '')}, lost {lost}, errors {bus.get('bus_err', 0)}, "
                          f"repeats {bus.get('repeats', 0)}, bad {bus.get('bad_values', 0)}, "
                          f"hint {bus.get('hint')}", flush=True)
                    if bus.get("hint") != last_hint:
                        last_hint = bus.get("hint")
                        if last_hint != "ok":
                            print(f"         {bus.get('hint_text')}")
                    if out:
                        data["_at"] = dt.datetime.now().isoformat(timespec="seconds")
                        with open(out / "diag.jsonl", "a", encoding="utf-8") as fh:
                            fh.write(json.dumps(data) + "\n")
                time.sleep(args.every)
        except KeyboardInterrupt:
            pass
    return 0


def cmd_session(args: argparse.Namespace) -> int:
    """Boat mode, one process on the port: every log line to <out>/console.log
    (and the screen with --show-log), and every N seconds an NMEA DIAG whose
    JSON goes to <out>/diag.jsonl and <out>/diag-latest.json, with a status
    line and the hint whenever it changes."""
    out = Path(args.out or dt.datetime.now().strftime("sessions/%Y%m%d-%H%M%S"))
    out.mkdir(parents=True, exist_ok=True)
    print(f"# session in {out}/  (Ctrl-C stops)", file=sys.stderr)
    last_hint = None
    next_diag = 0.0
    with _open(args) as con, open(out / "console.log", "a", encoding="utf-8") as log:
        try:
            while True:
                now = time.monotonic()
                if now >= next_diag:
                    con.send("NMEA DIAG")
                    next_diag = now + args.every
                for line in con.lines(0.5):
                    log.write(f"{_stamp()} {line}\n")
                    if line.startswith('{"v":1'):
                        try:
                            data = json.loads(line)
                        except json.JSONDecodeError:
                            continue
                        data["_at"] = dt.datetime.now().isoformat(timespec="seconds")
                        with open(out / "diag.jsonl", "a", encoding="utf-8") as fh:
                            fh.write(json.dumps(data) + "\n")
                        (out / "diag-latest.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
                        bus, me = data.get("bus", {}), data.get("self", {})
                        lost = bus.get("lost_q", 0) + bus.get("lost_drv", 0) + bus.get("lost_hw", 0)
                        print(f"{_stamp()} {bus.get('state')} {bus.get('fps', 0)} f/s, "
                              f"{len(data.get('devices', []))} devices, node {me.get('node')} "
                              f"{me.get('address', '')}, lost {lost}, errors {bus.get('bus_err', 0)}, "
                              f"repeats {bus.get('repeats', 0)}, bad {bus.get('bad_values', 0)}, "
                              f"hint {bus.get('hint')}", flush=True)
                        if bus.get("hint") != last_hint:
                            last_hint = bus.get("hint")
                            if last_hint != "ok":
                                print(f"         {bus.get('hint_text')}", flush=True)
                    elif args.show_log:
                        print(f"{_stamp()} {line}", flush=True)
                    elif line.startswith("N2K:") or line.startswith("NMEA source"):
                        print(f"{_stamp()} {line}", flush=True)
                log.flush()
        except KeyboardInterrupt:
            pass
        except ConnectionError as exc:
            print(f"# {exc} (chip reset or cable out)", file=sys.stderr)
            return 1
    latest = out / "diag-latest.json"
    if latest.exists():
        print(f"\nLast snapshot ({latest}):\n")
        print(summarise_n2k(json.loads(latest.read_text())))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial device instead of finding it")
    ap.add_argument("--usb", action="store_true", help="use the USB socket, not UART")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("log")
    p.add_argument("--seconds", type=float, help="stop after this long")
    p.add_argument("--tee", help="also append to this file")
    p.set_defaults(fn=cmd_log)
    p = sub.add_parser("send")
    p.add_argument("words", nargs="+")
    p.add_argument("--seconds", type=float, default=2.0)
    p.set_defaults(fn=cmd_send)
    p = sub.add_parser("diag")
    p.add_argument("--out", help="save the JSON here")
    p.add_argument("--raw", action="store_true", help="print the JSON, not the summary")
    p.set_defaults(fn=cmd_diag)
    p = sub.add_parser("watch")
    p.add_argument("--every", type=float, default=10.0)
    p.add_argument("--out", help="directory for diag.jsonl")
    p.set_defaults(fn=cmd_watch)
    p = sub.add_parser("session", help="boat mode: full log plus a diagnostics snapshot every N s")
    p.add_argument("--every", type=float, default=10.0)
    p.add_argument("--out", help="session directory (default sessions/<date-time>)")
    p.add_argument("--show-log", action="store_true", help="print every log line, not just N2K ones")
    p.set_defaults(fn=cmd_session)
    args = ap.parse_args()
    try:
        return args.fn(args)
    except (LookupError, ConnectionError) as exc:
        print(exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

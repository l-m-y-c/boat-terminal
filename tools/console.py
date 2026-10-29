#!/usr/bin/env python3
"""The terminal's serial console: log, commands, NMEA DIAG snapshots.

    python3 tools/console.py log                 # stream the log (Ctrl-C stops)
    python3 tools/console.py log --tee boat.log  # and keep a copy
    python3 tools/console.py send NMEA STATS     # one command, print the reply
    python3 tools/console.py diag                # NMEA DIAG, summarised
    python3 tools/console.py diag --out diag.json
    python3 tools/console.py net [--src 35] [--refresh]   # the NMEA 2000 network
    python3 tools/console.py watch --every 10 --out session/   # snapshots only
    python3 tools/console.py session              # boat mode: log + snapshots, saved
    python3 tools/console.py dump --out nmea.txt  # the 0183 sentence + log ring

Mode-aware: it asks each plugged-in socket NMEA STATS. On the simulator the
log comes from the USB socket and commands go to the UART first; on NMEA 2000
only the UART socket exists. --usb / --port force a socket.
Console commands: NMEA SIM | N2K | STATS | DIAG | NET | OUT [n] | DEMO | PGN <n> [src=N] <hex>.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import sys
import time
from pathlib import Path

from lmyc_dev.diag import changes, find_json, status_line, summarise_device, summarise_n2k, summarise_network
from lmyc_dev import board
from lmyc_dev.ports import Console, find_port


def _stamp() -> str:
    return dt.datetime.now().strftime("%H:%M:%S")


def _open(args: argparse.Namespace, purpose: str = "command") -> Console:
    """The right socket for the mode: on the simulator the log is on USB and
    commands work on either; on NMEA 2000 only the UART works."""
    if args.port or args.usb:
        port = find_port("usb" if args.usb else "uart", args.port)
        print(f"# {port.describe()}", file=sys.stderr)
        return Console(port.path)
    sockets = board.survey()
    chosen = board.pick(sockets, purpose)
    mode = board.board_mode(sockets)
    if chosen is None:
        notes = board.explain(sockets) or ["No terminal answered NMEA STATS."]
        raise LookupError("\n".join(notes + ["Try `python3 tools/board.py status`."]))
    print(f"# {chosen.port.describe()}  [{mode}]", file=sys.stderr)
    return Console(chosen.port.path)


def cmd_log(args: argparse.Namespace) -> int:
    tee = open(args.tee, "a", encoding="utf-8") if args.tee else None
    try:
        with _open(args, "log") as con:
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


def cmd_net(args: argparse.Namespace) -> int:
    """NMEA NET: the topology, or one device with --src."""
    line = "NMEA NET" + (f" {args.src}" if args.src is not None else "") + (" REFRESH" if args.refresh else "")
    with _open(args) as con:
        lines = con.command(line, 4.0)
        if args.refresh:
            time.sleep(3)
            lines = con.command("NMEA NET", 4.0)
    data = find_json(lines)
    if data is None:
        print("\n".join(l for l in lines if l.startswith("NMEA")) or "No network JSON (older firmware?)")
        return 1
    print(json.dumps(data, indent=2) if args.raw else (summarise_device(data) if "device" in data else summarise_network(data)))
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
                    bus = data.get("bus", {})
                    print(f"{_stamp()} {status_line(data)}", flush=True)
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


def _reopen(args: argparse.Namespace, seconds: float = 60.0) -> Console | None:
    """After a reboot or a cable out: wait for a terminal socket that answers."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        time.sleep(2)
        try:
            return _open(args)
        except (LookupError, ConnectionError, OSError):
            continue
    return None


def cmd_session(args: argparse.Namespace) -> int:
    """Boat mode, one process on the port: every log line to <out>/console.log
    (and the screen with --show-log), and every N seconds an NMEA DIAG whose
    JSON goes to <out>/diag.jsonl and <out>/diag-latest.json, with a status
    line (rates, devices, bus errors, bus-off) and what changed: new devices,
    new PGN/source pairs, duplicate sources, our address, a reboot.
    A reboot or a cable pulled for a moment does not end it: it reopens."""
    out = Path(args.out or dt.datetime.now().strftime("sessions/%Y%m%d-%H%M%S"))
    out.mkdir(parents=True, exist_ok=True)
    print(f"# session in {out}/  (Ctrl-C stops)", file=sys.stderr)
    last_hint = None
    prev: dict | None = None
    next_diag = 0.0
    con = _open(args)
    log = open(out / "console.log", "a", encoding="utf-8")
    try:
        while True:
            try:
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
                        print(f"{_stamp()} {status_line(data)}", flush=True)
                        for c in changes(prev, data):
                            print(f"         {c}", flush=True)
                            log.write(f"{_stamp()} # {c}\n")
                        prev = data
                        hint = data.get("bus", {}).get("hint")
                        if hint != last_hint:
                            last_hint = hint
                            if hint != "ok":
                                print(f"         {data.get('bus', {}).get('hint_text')}", flush=True)
                    elif args.show_log:
                        print(f"{_stamp()} {line}", flush=True)
                    elif line.startswith(("N2K:", "NMEA source", "=== LMYC", "LINK")):
                        print(f"{_stamp()} {line}", flush=True)
                log.flush()
            except (ConnectionError, OSError) as exc:
                print(f"{_stamp()} # {exc} (reboot or cable out), waiting for the terminal...", file=sys.stderr, flush=True)
                log.write(f"{_stamp()} # port lost: {exc}\n")
                con.close()
                con = _reopen(args, args.wait)
                if con is None:
                    print(f"# no terminal for {args.wait:.0f} s, stopping", file=sys.stderr)
                    return 1
                log.write(f"{_stamp()} # port back\n")
                next_diag = 0.0
    except KeyboardInterrupt:
        pass
    finally:
        if con is not None:
            con.close()
        log.close()
    latest = out / "diag-latest.json"
    if latest.exists():
        print(f"\nLast snapshot ({latest}):\n")
        print(summarise_n2k(json.loads(latest.read_text())))
    return 0


def cmd_dump(args: argparse.Namespace) -> int:
    """NMEA OUT: the terminal's 0183 sentence + log ring over the UART."""
    body: list[str] = []
    inside = False
    with _open(args) as con:
        for _ in con.lines(0.1):
            pass
        con.send(f"NMEA OUT {args.n}")
        for l in con.lines(args.seconds):
            if l.startswith("NMEA OUT BEGIN"):
                inside, body = True, []
            elif l.startswith("NMEA OUT END"):
                break
            elif inside:
                body.append(l)
    if not body:
        print("No NMEA OUT block in the reply (firmware older than the diagnostics ring?)")
        return 1
    text = "\n".join(body) + "\n"
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
        print(f"Saved {args.out}: {len(body)} entries")
    else:
        sys.stdout.write(text)
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
    p = sub.add_parser("net", help="the NMEA 2000 network (NMEA NET), or one device")
    p.add_argument("--src", type=int, help="one device's details")
    p.add_argument("--refresh", action="store_true", help="ask every device again first")
    p.add_argument("--raw", action="store_true")
    p.add_argument("--out")
    p.set_defaults(fn=cmd_net)
    p = sub.add_parser("dump", help="the 0183 sentence + log ring (NMEA OUT)")
    p.add_argument("-n", type=int, default=8192)
    p.add_argument("--seconds", type=float, default=90.0)
    p.add_argument("--out")
    p.set_defaults(fn=cmd_dump)
    p = sub.add_parser("watch")
    p.add_argument("--every", type=float, default=10.0)
    p.add_argument("--out", help="directory for diag.jsonl")
    p.set_defaults(fn=cmd_watch)
    p = sub.add_parser("session", help="boat mode: full log plus a diagnostics snapshot every N s")
    p.add_argument("--every", type=float, default=10.0)
    p.add_argument("--out", help="session directory (default sessions/<date-time>)")
    p.add_argument("--show-log", action="store_true", help="print every log line, not just N2K ones")
    p.add_argument("--wait", type=float, default=120.0, help="how long to wait for the terminal after a reboot")
    p.set_defaults(fn=cmd_session)
    args = ap.parse_args()
    try:
        return args.fn(args)
    except (LookupError, ConnectionError) as exc:
        print(exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

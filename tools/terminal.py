#!/usr/bin/env python3
"""The terminal over its hotspot: status, bus check, recording, 0183 stream.

    python3 tools/terminal.py status                 # GET /status.json
    python3 tools/terminal.py bus                    # GET /n2k.json, summarised
    python3 tools/terminal.py bus --out bus.json
    python3 tools/terminal.py network [--refresh]   # every device on the bus
    python3 tools/terminal.py device 35              # one device in detail
    python3 tools/terminal.py record --seconds 120 --out dock.log
    python3 tools/terminal.py nmea --seconds 30      # TCP 10110: what chart apps get
    python3 tools/terminal.py fetch-diag --out DIR   # rings, bus picture, network, TCP sample

Default host 192.168.4.1 (a laptop on the boat Wi-Fi). Through the phone's
relay use --host 127.0.0.1:10180 (HTTP) and --nmea-host 127.0.0.1.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import datetime as dt
import json
import socket
import sys
import time
import urllib.error
import urllib.request
from collections import Counter

from lmyc_dev.diag import summarise_device, summarise_n2k, summarise_network, summarise_status


def _get(host: str, path: str, timeout: float = 4.0) -> str:
    with urllib.request.urlopen(f"http://{host}{path}", timeout=timeout) as r:
        return r.read().decode("utf-8", errors="replace")


def socket_note(src: str | None) -> str:
    """Which USB-C socket a laptop can use, for the mode the terminal reports."""
    if src is None:
        return "Mode unknown (older firmware)."
    if "2000" in src or src.lower() == "n2k":
        return ("Mode NMEA 2000: only the UART socket has a console (the USB socket's pins are "
                "the CAN bus). Battery-only laptop on the UART socket while on the backbone.")
    return ("Mode Simulator: the log is on the USB socket (and mirrored to UART on new firmware); "
            "commands work on either.")


def cmd_status(args: argparse.Namespace) -> int:
    d = json.loads(_get(args.host, "/status.json"))
    print(json.dumps(d, indent=2) if args.raw else summarise_status(d))
    return 0


def cmd_bus(args: argparse.Namespace) -> int:
    body = _get(args.host, "/n2k.json")
    d = json.loads(body)
    if d.get("warming"):
        print("The terminal is still starting its diagnostics. Try again in a second.")
        return 1
    print(json.dumps(d, indent=2) if args.raw else summarise_n2k(d))
    if not args.raw:
        print("\n" + socket_note(d.get("bus", {}).get("src")))
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(body)
        print(f"\nSaved {args.out}")
    return 0


def cmd_network(args: argparse.Namespace) -> int:
    body = _get(args.host, "/n2k/network.json" + ("?refresh=1" if args.refresh else ""))
    if args.refresh:
        time.sleep(3)
        body = _get(args.host, "/n2k/network.json")
    d = json.loads(body)
    print(json.dumps(d, indent=2) if args.raw else summarise_network(d))
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(body)
    return 0


def cmd_device(args: argparse.Namespace) -> int:
    d = json.loads(_get(args.host, f"/n2k/device.json?src={args.src}"))
    print(json.dumps(d, indent=2) if args.raw else summarise_device(d))
    return 0


def cmd_record(args: argparse.Namespace) -> int:
    """Poll /n2k/frames like the phone's Record bus, into a candump log."""
    after, frames, lost = 0, 0, 0
    deadline = None if args.seconds is None else time.monotonic() + args.seconds
    with open(args.out, "w", encoding="utf-8") as fh:
        fh.write("# LMYC boat terminal bus capture, candump log format. "
                 "Times are seconds since the terminal started.\n")
        fh.write(f"# started {dt.datetime.now().isoformat(timespec='seconds')} via tools/terminal.py\n")
        try:
            while deadline is None or time.monotonic() < deadline:
                try:
                    src = f"&src={args.src}" if args.src is not None else ""
                    body = _get(args.host, f"/n2k/frames?after={after}{src}")
                except (urllib.error.URLError, OSError) as exc:
                    print(f"\r{exc}", file=sys.stderr)
                    time.sleep(1)
                    continue
                rows = body.splitlines()
                head = rows[0].split() if rows else []
                if len(head) < 6 or head[0] != "#next":
                    print("Unexpected answer from /n2k/frames", file=sys.stderr)
                    return 1
                after, chunk_lost, more = int(head[1]), int(head[3]), head[5] == "1"
                if chunk_lost:
                    lost += chunk_lost
                    fh.write(f"# {chunk_lost} frames lost here\n")
                for r in rows[1:]:
                    if r:
                        fh.write(r + "\n")
                        frames += 1
                print(f"\r{frames} frames, {lost} lost", end="", file=sys.stderr, flush=True)
                if not more:
                    time.sleep(0.5)
        except KeyboardInterrupt:
            pass
        fh.write(f"# stopped {dt.datetime.now().isoformat(timespec='seconds')}: {frames} frames, {lost} lost\n")
    print(f"\nSaved {args.out}: {frames} frames, {lost} lost")
    return 0


def _checksum_ok(line: str) -> bool:
    star = line.rfind("*")
    if star < 1 or len(line) < star + 3 or line[0] not in "$!":
        return False
    cs = 0
    for ch in line[1:star]:
        cs ^= ord(ch)
    try:
        return cs == int(line[star + 1:star + 3], 16)
    except ValueError:
        return False


def cmd_nmea(args: argparse.Namespace) -> int:
    """What a chart app receives on TCP 10110: per-sentence counts and rates,
    checksum failures, and the latest of each."""
    counts: Counter[str] = Counter()
    latest: dict[str, str] = {}
    bad = 0
    long = 0
    start = time.monotonic()
    tee = open(args.out, "w", encoding="utf-8") if args.out else None
    try:
        with socket.create_connection((args.nmea_host, 10110), timeout=5) as s:
            s.settimeout(1.0)
            buf = b""
            while args.seconds is None or time.monotonic() - start < args.seconds:
                try:
                    data = s.recv(4096)
                except socket.timeout:
                    continue
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = raw.decode("ascii", errors="replace").strip()
                    if not line:
                        continue
                    if tee:
                        tee.write(line + "\n")
                    if len(line) > 82:
                        long += 1
                    if not _checksum_ok(line):
                        bad += 1
                        continue
                    key = line[1:6]
                    counts[key] += 1
                    latest[key] = line
    except KeyboardInterrupt:
        pass
    except OSError as exc:
        print(f"TCP {args.nmea_host}:10110: {exc}")
        return 1
    finally:
        if tee:
            tee.close()
    secs = max(time.monotonic() - start, 0.001)
    print(f"{sum(counts.values())} sentences in {secs:.0f} s, {bad} bad checksums, {long} over 82 chars")
    for key in sorted(counts):
        print(f"  {key}  {counts[key] / secs:5.2f}/s  {latest[key]}")
    return 0 if bad == 0 else 1


def _drain(host: str, path: str, after: int, timeout: float = 6.0) -> tuple[list[str], int, int]:
    """Every line of a "#next N lost M more 0|1" endpoint from `after` on."""
    rows_all: list[str] = []
    lost = 0
    for _ in range(2000):
        body = _get(host, f"{path}{'&' if '?' in path else '?'}after={after}", timeout)
        rows = body.splitlines()
        head = rows[0].split() if rows else []
        if len(head) < 6 or head[0] != "#next":
            raise ValueError(f"unexpected answer from {path}")
        nxt, chunk_lost, more = int(head[1]), int(head[3]), head[5] == "1"
        lost += chunk_lost
        rows_all.extend(r for r in rows[1:] if r)
        if nxt <= after or not more:
            break
        after = nxt
    return rows_all, lost, after


def cmd_fetch_diag(args: argparse.Namespace) -> int:
    """Everything the terminal keeps for the bench, into one folder: status,
    bus picture, network, the whole capture ring (incl. the terminal's own
    frames), the 0183 sentence + log ring, and a few seconds of TCP 10110."""
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    ok = True
    for name, path in (("status.json", "/status.json"), ("n2k.json", "/n2k.json"),
                       ("network.json", "/n2k/network.json")):
        try:
            (out / name).write_text(_get(args.host, path, 8.0), encoding="utf-8")
            print(f"saved {name}")
        except (urllib.error.URLError, OSError) as exc:
            print(f"{name}: {exc}", file=sys.stderr)
            ok = False
    try:
        rows, lost, _ = _drain(args.host, "/n2k/frames", 1)
        with open(out / "bus-ring.log", "w", encoding="utf-8") as fh:
            fh.write("# LMYC boat terminal capture ring (incl. its own TX), candump log format. "
                     "Times are seconds since the terminal started.\n")
            fh.write(f"# fetched {dt.datetime.now().isoformat(timespec='seconds')}, {lost} older frames overwritten\n")
            fh.write("\n".join(rows) + ("\n" if rows else ""))
        print(f"saved bus-ring.log: {len(rows)} frames")
    except (urllib.error.URLError, OSError, ValueError) as exc:
        print(f"bus ring: {exc}", file=sys.stderr)
        ok = False
    try:
        rows, lost, _ = _drain(args.host, "/diag/nmea.txt", 0)
        (out / "nmea-ring.txt").write_text("\n".join(rows) + ("\n" if rows else ""), encoding="utf-8")
        print(f"saved nmea-ring.txt: {len(rows)} entries (S sentence, E bus event, L log)")
    except (urllib.error.URLError, OSError, ValueError) as exc:
        print(f"nmea ring: {exc} (firmware older than the diagnostics ring?)", file=sys.stderr)
    if args.tcp_seconds > 0:
        host = args.nmea_host or args.host.split(":")[0]
        lines: list[str] = []
        try:
            with socket.create_connection((host, 10110), timeout=4) as s:
                s.settimeout(1.0)
                end = time.monotonic() + args.tcp_seconds
                buf = b""
                while time.monotonic() < end:
                    try:
                        chunk = s.recv(4096)
                    except socket.timeout:
                        continue
                    if not chunk:
                        break
                    buf += chunk
            lines = buf.decode("ascii", "replace").splitlines()
            (out / "tcp10110.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
            print(f"saved tcp10110.txt: {len(lines)} lines in {args.tcp_seconds} s")
        except OSError as exc:
            print(f"tcp 10110: {exc}", file=sys.stderr)
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="192.168.4.1", help="HTTP host[:port]")
    ap.add_argument("--nmea-host", default=None, help="TCP 10110 host (default: the --host address)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("status")
    p.add_argument("--raw", action="store_true")
    p.set_defaults(fn=cmd_status)
    p = sub.add_parser("bus")
    p.add_argument("--raw", action="store_true")
    p.add_argument("--out")
    p.set_defaults(fn=cmd_bus)
    p = sub.add_parser("network")
    p.add_argument("--refresh", action="store_true")
    p.add_argument("--raw", action="store_true")
    p.add_argument("--out")
    p.set_defaults(fn=cmd_network)
    p = sub.add_parser("device")
    p.add_argument("src", type=int)
    p.add_argument("--raw", action="store_true")
    p.set_defaults(fn=cmd_device)
    p = sub.add_parser("record")
    p.add_argument("--seconds", type=float)
    p.add_argument("--src", type=int, help="only this device's frames")
    p.add_argument("--out", required=True)
    p.set_defaults(fn=cmd_record)
    p = sub.add_parser("fetch-diag", help="everything the terminal keeps, into one folder")
    p.add_argument("--out", required=True)
    p.add_argument("--tcp-seconds", type=float, default=10.0)
    p.set_defaults(fn=cmd_fetch_diag)
    p = sub.add_parser("nmea")
    p.add_argument("--seconds", type=float, default=30.0)
    p.add_argument("--out", help="also save every line")
    p.set_defaults(fn=cmd_nmea)
    args = ap.parse_args()
    if args.nmea_host is None:
        args.nmea_host = args.host.split(":")[0]
    try:
        return args.fn(args)
    except (urllib.error.URLError, OSError) as exc:
        print(f"Cannot reach the terminal at {args.host}: {exc}\n"
              "Join the boat Wi-Fi (the terminal's hotspot) first.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

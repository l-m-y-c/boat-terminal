#!/usr/bin/env python3
"""The terminal over its hotspot: status, bus check, recording, 0183 stream.

    python3 tools/terminal.py status                 # GET /status.json
    python3 tools/terminal.py bus                    # GET /n2k.json, summarised
    python3 tools/terminal.py bus --out bus.json
    python3 tools/terminal.py network [--refresh]   # every device on the bus
    python3 tools/terminal.py device 35              # one device in detail
    python3 tools/terminal.py record --seconds 120 --out dock.log
    python3 tools/terminal.py nmea --seconds 30      # TCP 10110: what chart apps get

Default host 192.168.4.1 (a laptop on the boat Wi-Fi). Through the phone's
relay use --host 127.0.0.1:10180 (HTTP) and --nmea-host 127.0.0.1.
"""

from __future__ import annotations

import argparse
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


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="192.168.4.1", help="HTTP host[:port]")
    ap.add_argument("--nmea-host", default="192.168.4.1", help="TCP 10110 host")
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
    p = sub.add_parser("nmea")
    p.add_argument("--seconds", type=float, default=30.0)
    p.add_argument("--out", help="also save every line")
    p.set_defaults(fn=cmd_nmea)
    args = ap.parse_args()
    try:
        return args.fn(args)
    except (urllib.error.URLError, OSError) as exc:
        print(f"Cannot reach the terminal at {args.host}: {exc}\n"
              "Join the boat Wi-Fi (the terminal's hotspot) first.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

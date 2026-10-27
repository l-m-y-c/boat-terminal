#!/usr/bin/env python3
"""Look inside a bus recording (candump log from the phone's Record bus,
tools/terminal.py record, or replay --sim).

    python3 tools/capture.py summary dock.log            # devices, PGNs, fast packets
    python3 tools/capture.py summary dock.log --terminal 23   # also check what it sent
    python3 tools/capture.py frames dock.log --pgn 128267 [--src 35]
    python3 tools/capture.py claims dock.log              # address claims, decoded
    python3 tools/capture.py replay dock.log [--sentences out.txt] [--json out.json]
    python3 tools/capture.py sim 60 sim.log               # a capture from the simulator

summary reads the frames itself; replay runs them through the terminal's
real library, diagnostics and converter on the host (firmware/test_host/replay.sh).
"""

from __future__ import annotations

import argparse
import subprocess
import sys

from lmyc_dev import FIRMWARE
from lmyc_dev.n2k import PGN_NAMES, analyse, format_line, read_capture, report


def cmd_summary(args: argparse.Namespace) -> int:
    frames, notes = read_capture(args.file)
    for n in notes:
        print(f"# {n}")
    if not frames:
        print("No frames in the file.")
        return 1
    print(report(analyse(frames), args.terminal))
    return 0


def cmd_frames(args: argparse.Namespace) -> int:
    frames, _ = read_capture(args.file)
    shown = 0
    for f in frames:
        if args.pgn is not None and f.pgn != args.pgn:
            continue
        if args.src is not None and f.src != args.src:
            continue
        print(f"{format_line(f)}   pgn {f.pgn} {PGN_NAMES.get(f.pgn, '')} src {f.src} dst {f.dst} prio {f.priority}")
        shown += 1
        if args.limit and shown >= args.limit:
            break
    return 0


def cmd_claims(args: argparse.Namespace) -> int:
    frames, _ = read_capture(args.file)
    a = analyse(frames)
    if not a.claims:
        print("No address claims in this capture. Record while devices power up, or while "
              "the plotter asks for them.")
        return 1
    for t, src, name in a.claims:
        print(f"{t:10.3f}  address {src:3d}  {name.describe()}  NAME {name.raw:016X}")
    return 0


def _replay(extra: list[str]) -> int:
    return subprocess.call(["sh", str(FIRMWARE / "test_host" / "replay.sh"), *extra])


def cmd_replay(args: argparse.Namespace) -> int:
    extra = [args.file]
    if args.sentences:
        extra += ["--sentences", args.sentences]
    if args.json:
        extra += ["--json", args.json]
    return _replay(extra)


def cmd_sim(args: argparse.Namespace) -> int:
    return _replay(["--sim", str(args.seconds), "--write", args.out])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("summary")
    p.add_argument("file")
    p.add_argument("--terminal", type=int, help="the terminal's address: check it sent only network management")
    p.set_defaults(fn=cmd_summary)
    p = sub.add_parser("frames")
    p.add_argument("file")
    p.add_argument("--pgn", type=int)
    p.add_argument("--src", type=int)
    p.add_argument("--limit", type=int, default=0)
    p.set_defaults(fn=cmd_frames)
    p = sub.add_parser("claims")
    p.add_argument("file")
    p.set_defaults(fn=cmd_claims)
    p = sub.add_parser("replay")
    p.add_argument("file")
    p.add_argument("--sentences")
    p.add_argument("--json")
    p.set_defaults(fn=cmd_replay)
    p = sub.add_parser("sim")
    p.add_argument("seconds", type=int)
    p.add_argument("out")
    p.set_defaults(fn=cmd_sim)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""A synthetic Garmin GPSMAP 740s capture (candump log), for bench tests.

What a Garmin chart plotter with its internal GPS puts on NMEA 2000 when it
is the only other device on the backbone (Baca Lele, docs/27-baca-lele-garmin-740s.md): address claim,
129025 position rapid (10 Hz), 129026 COG/SOG rapid (4 Hz, true reference),
129029 GNSS position (1 Hz), 126992 system time (1 Hz), 127258 magnetic
variation (1 Hz), 129539 DOPs and 129540 satellites in view (1 Hz). No
heading, depth or wind: the 740s has no such sensors.

    python3 tools/garmin740s_capture.py 30 garmin.log
    python3 tools/garmin740s_capture.py 30 garmin.log --no-129029 --cog-ref magnetic --at-rest

This is a model, not a recording: replace it with a real capture when one
exists (sessions/<date>/ from make boat-diag).
"""
import argparse
import math
import struct
import sys

SRC = 1  # Garmin plotters usually claim a low address
LAT0, LON0 = 49.9917, -124.7600  # Desolation Sound approach, illustrative
DAY0, SEC0 = 20755, 64800.0      # 2026-10-29 18:00:00 UTC


def can_id(prio, pgn, src, dst=0xFF):
    pf = (pgn >> 8) & 0xFF
    if pf < 240:
        pgn = (pgn & 0x3FF00) | dst
    return (prio << 26) | (pgn << 8) | src


class Writer:
    def __init__(self, out):
        self.out = out
        self.seq = {}

    def frame(self, t, cid, data):
        self.out.write("(%.6f) can0 %08X#%s\n" % (t, cid, bytes(data).hex().upper()))

    def single(self, t, prio, pgn, data):
        data = bytes(data) + b"\xff" * (8 - len(data))
        self.frame(t, can_id(prio, pgn, SRC), data[:8])

    def fast(self, t, prio, pgn, data):
        s = self.seq.get(pgn, 0)
        self.seq[pgn] = (s + 1) & 7
        cid = can_id(prio, pgn, SRC)
        data = bytes(data)
        chunks = [bytes([s << 5, len(data)]) + data[:6]]
        rest = data[6:]
        n = 1
        while rest:
            chunks.append(bytes([(s << 5) | n]) + rest[:7])
            rest = rest[7:]
            n += 1
        for i, c in enumerate(chunks):
            self.frame(t + i * 0.0005, cid, c + b"\xff" * (8 - len(c)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("seconds", type=float)
    ap.add_argument("out")
    ap.add_argument("--no-129029", action="store_true", help="the plotter sends only the rapid PGNs")
    ap.add_argument("--no-time", action="store_true", help="no 126992")
    ap.add_argument("--cog-ref", choices=["true", "magnetic"], default="true")
    ap.add_argument("--at-rest", action="store_true", help="SOG 0, COG not available (at the dock)")
    a = ap.parse_args()

    sog_ms = 0.0 if a.at_rest else 5.5 * 0.514444
    cog = 1.0  # rad
    out = open(a.out, "w") if a.out != "-" else sys.stdout
    out.write("# synthetic Garmin GPSMAP 740s, src %d, %.0f s (tools/garmin740s_capture.py)\n" % (SRC, a.seconds))
    w = Writer(out)
    # ISO address claim: unique 0x12345, Garmin (229), class 120 display, function 130
    name = (0x12345) | (229 << 21) | (0 << 32) | (130 << 40) | (120 << 49) | (4 << 60) | (1 << 63)
    w.frame(0.0, can_id(6, 60928, SRC), struct.pack("<Q", name))
    steps = int(a.seconds * 10)
    sid = 0
    for k in range(steps):
        t = 0.1 + k * 0.1
        dist = sog_ms * t
        lat = LAT0 + dist * math.cos(cog) / 111111.0
        lon = LON0 + dist * math.sin(cog) / (111111.0 * math.cos(math.radians(LAT0)))
        w.single(t, 2, 129025, struct.pack("<ii", round(lat * 1e7), round(lon * 1e7)))
        if k % 10 == 0:
            sid = (sid + 1) % 250
        if k % 25 in (0,) or k % 10 in (0, 2, 5, 7):
            ref = 0 if a.cog_ref == "true" else 1
            cogv = 0xFFFF if a.at_rest else round(cog * 1e4)
            w.single(t + 0.002, 2, 129026,
                     struct.pack("<BBHH", sid, 0xFC | ref, cogv, round(sog_ms * 100)) + b"\xff\xff")
        if k % 10 == 0:
            secs = SEC0 + t
            if not a.no_time:
                w.single(t + 0.004, 3, 126992, struct.pack("<BBHI", sid, 0xF0, DAY0, round(secs * 1e4)))
            w.single(t + 0.006, 6, 127258,
                     struct.pack("<BBHh", sid, 0xF1, DAY0, round(math.radians(-17.0) * 1e4)) + b"\xff\xff")
            if not a.no_129029:
                body = struct.pack("<BHIqqq", sid, DAY0, round(secs * 1e4), round(lat * 1e16), round(lon * 1e16),
                                   round(12.0 * 1e6))
                body += bytes([0x10 | 0x00, 0xFC, 9]) + struct.pack("<hhi", 90, 160, round(-17.0 * 100)) + bytes([0])
                w.fast(t + 0.008, 3, 129029, body)
            w.fast(t + 0.02, 6, 129539, struct.pack("<BBBhhh", sid, 0x03 | 0x18, 0xFF, 90, 110, 160))
    out.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""NMEA 2000 helpers: candump logs, CAN ids, NAMEs, and capture analysis.

The terminal records frames as candump log lines, from the phone's Record
bus or GET /n2k/frames:

    (12.345678) can0 09F80123#0102030405060708

Names and manufacturer codes match firmware/src/n2k_diag.cpp.
"""

from __future__ import annotations

import re
from collections import defaultdict
from dataclasses import dataclass, field

_LINE = re.compile(r"^\((\d+(?:\.\d+)?)\)\s+\S+\s+([0-9A-Fa-f]{1,8})#([0-9A-Fa-f]*)\s*$")

# The library treats these as fast packet (firmware n2k_diag and the
# NMEA2000 library's default list, for the PGNs the terminal cares about).
FAST_PACKET = {
    126208, 126464, 126720, 126996, 126998, 127233, 127237, 127489, 127496, 127497, 127498,
    127503, 127504, 127506, 127507, 127509, 127510, 127511, 127512, 127513, 127514,
    128275, 128520, 129029, 129038, 129039, 129040, 129041, 129044, 129045, 129284,
    129285, 129301, 129302, 129538, 129540, 129541, 129542, 129545, 129547, 129549,
    129551, 129556, 129792, 129793, 129794, 129795, 129796, 129797, 129798, 129799,
    129800, 129801, 129802, 129803, 129804, 129805, 129806, 129807, 129808, 129809,
    129810, 130052, 130053, 130054, 130060, 130061, 130064, 130065, 130066, 130067,
    130068, 130069, 130070, 130071, 130072, 130073, 130074, 130320, 130321, 130322,
    130323, 130324, 130567, 130577, 130578, 130816,
}

PGN_NAMES = {
    59392: "ISO Ack", 59904: "ISO Request", 60928: "Address claim", 65240: "Commanded address",
    126208: "Group function", 126464: "PGN list", 126720: "Proprietary", 126992: "System time",
    126993: "Heartbeat", 126996: "Product info", 126998: "Config info", 127245: "Rudder",
    127250: "Heading", 127251: "Rate of turn", 127257: "Attitude", 127258: "Variation",
    127488: "Engine rapid", 127489: "Engine", 127505: "Fluid level", 127508: "Battery",
    128259: "Water speed", 128267: "Depth", 128275: "Distance log", 129025: "Position rapid",
    129026: "COG/SOG rapid", 129029: "GNSS position", 129033: "Time & date",
    129038: "AIS A position", 129039: "AIS B position", 129040: "AIS B extended",
    129283: "Cross track", 129284: "Navigation", 129285: "Route/WP", 129291: "Set & drift",
    129539: "GNSS DOPs", 129540: "GNSS sats", 129794: "AIS A static", 129808: "DSC",
    129809: "AIS B static A", 129810: "AIS B static B", 130306: "Wind", 130310: "Environment",
    130311: "Environment", 130312: "Temperature", 130313: "Humidity", 130314: "Pressure",
    130316: "Temperature ext",
}

MAKERS = {
    135: "Airmar", 137: "Maretron", 140: "Lowrance", 144: "Mercury", 174: "Volvo Penta",
    229: "Garmin", 273: "Actisense", 275: "Navico", 315: "Icom", 355: "Mastervolt",
    358: "Victron", 381: "B&G", 419: "Fusion", 421: "Standard Horizon", 437: "Digital Yacht",
    504: "Vesper", 717: "Yacht Devices", 1851: "Raymarine", 1853: "JRC", 1855: "Furuno",
    1857: "Simrad", 1862: "Yamaha", 2046: "open source (2046)",
}

CLASSES = {
    10: "System tools", 20: "Safety", 25: "Gateway", 30: "Electrical", 35: "Generation",
    40: "Steering", 50: "Propulsion", 60: "Navigation", 70: "Communication",
    75: "Sensor interface", 80: "Instrumentation", 85: "Environment",
    90: "Internal environment", 110: "Human interface", 120: "Display", 125: "Entertainment",
}

# The network-management PGNs a standard node sends. The terminal must
# never send anything else.
MANAGEMENT = {59392, 59904, 60928, 65240, 126208, 126464, 126993, 126996, 126998}


@dataclass
class Frame:
    t: float
    can_id: int
    data: bytes

    @property
    def pgn(self) -> int:
        return pgn_of(self.can_id)

    @property
    def src(self) -> int:
        return self.can_id & 0xFF

    @property
    def dst(self) -> int:
        pf = (self.can_id >> 16) & 0xFF
        return (self.can_id >> 8) & 0xFF if pf < 240 else 0xFF

    @property
    def priority(self) -> int:
        return (self.can_id >> 26) & 0x7


def pgn_of(can_id: int) -> int:
    pf = (can_id >> 16) & 0xFF
    pgn = (can_id >> 8) & 0x3FFFF
    return pgn & 0x3FF00 if pf < 240 else pgn


def can_id(priority: int, pgn: int, src: int, dst: int = 0xFF) -> int:
    pf = (pgn >> 8) & 0xFF
    if pf < 240:
        pgn = (pgn & 0x3FF00) | dst
    return ((priority & 7) << 26) | ((pgn & 0x3FFFF) << 8) | (src & 0xFF)


def parse_line(line: str) -> Frame | None:
    m = _LINE.match(line.strip())
    if not m:
        return None
    hexdata = m.group(3)
    if len(hexdata) % 2 or len(hexdata) > 16:
        return None
    return Frame(float(m.group(1)), int(m.group(2), 16) & 0x1FFFFFFF, bytes.fromhex(hexdata))


def format_line(f: Frame) -> str:
    return f"({f.t:.6f}) can0 {f.can_id:08X}#{f.data.hex().upper()}"


def read_capture(path: str) -> tuple[list[Frame], list[str]]:
    """Frames, and the capture's own '#' notes (losses, start and stop)."""
    frames: list[Frame] = []
    notes: list[str] = []
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if line.startswith("#"):
                notes.append(line[1:].strip())
                continue
            f = parse_line(line)
            if f is not None:
                frames.append(f)
    return frames, notes


@dataclass
class Name:
    raw: int

    unique = property(lambda s: s.raw & 0x1FFFFF)
    manufacturer = property(lambda s: (s.raw >> 21) & 0x7FF)
    device_instance = property(lambda s: (s.raw >> 32) & 0xFF)
    function = property(lambda s: (s.raw >> 40) & 0xFF)
    device_class = property(lambda s: (s.raw >> 49) & 0x7F)
    system_instance = property(lambda s: (s.raw >> 56) & 0xF)
    industry = property(lambda s: (s.raw >> 60) & 0x7)
    arbitrary = property(lambda s: bool(s.raw >> 63))

    @staticmethod
    def from_bytes(data: bytes) -> "Name":
        return Name(int.from_bytes(data[:8], "little"))

    def describe(self) -> str:
        maker = MAKERS.get(self.manufacturer, f"maker {self.manufacturer}")
        cls = CLASSES.get(self.device_class, f"class {self.device_class}")
        return (f"{maker}, {cls} (class {self.device_class} / function {self.function}), "
                f"unique {self.unique}, instance {self.device_instance}")


@dataclass
class PgnStats:
    frames: int = 0
    starts: int = 0
    whole: int = 0
    broken: int = 0
    first_t: float = 0.0
    last_t: float = 0.0


@dataclass
class Device:
    src: int
    frames: int = 0
    name: Name | None = None
    model: str = ""
    pgns: set[int] = field(default_factory=set)


@dataclass
class Analysis:
    frames: int = 0
    start: float = 0.0
    end: float = 0.0
    repeats: int = 0
    pgns: dict[tuple[int, int], PgnStats] = field(default_factory=dict)
    devices: dict[int, Device] = field(default_factory=dict)
    claims: list[tuple[float, int, Name]] = field(default_factory=list)
    requests: list[tuple[float, int, int, int]] = field(default_factory=list)

    @property
    def seconds(self) -> float:
        return max(self.end - self.start, 0.0)


def analyse(frames: list[Frame]) -> Analysis:
    """Per-PGN rates, fast-packet completion, repeats, address claims,
    product information model IDs and ISO requests."""
    a = Analysis()
    if not frames:
        return a
    a.frames = len(frames)
    a.start, a.end = frames[0].t, frames[-1].t
    # Fast packets being reassembled, per (PGN, source).
    open_fp: dict[tuple[int, int], _Partial] = {}
    prev: Frame | None = None
    for f in frames:
        if prev and f.can_id == prev.can_id and f.data == prev.data and f.t - prev.t < 0.002:
            a.repeats += 1
        prev = f
        key = (f.pgn, f.src)
        st = a.pgns.setdefault(key, PgnStats(first_t=f.t))
        st.frames += 1
        st.last_t = f.t
        dev = a.devices.setdefault(f.src, Device(f.src))
        dev.frames += 1
        dev.pgns.add(f.pgn)
        message: bytes | None = None
        if f.pgn in FAST_PACKET:
            if not f.data:
                continue
            seq, idx = f.data[0] >> 5, f.data[0] & 0x1F
            if idx == 0:
                if key in open_fp:
                    st.broken += 1  # a new message started before the last one finished
                    del open_fp[key]
                if len(f.data) < 2:
                    continue
                st.starts += 1
                part = _Partial(seq, 1, f.data[1], bytearray(f.data[2:]))
                if len(part.buf) >= part.total:
                    message = bytes(part.buf[: part.total])
                else:
                    open_fp[key] = part
            else:
                part = open_fp.get(key)
                if part is None:
                    continue  # orphan frame: its start was before the capture
                if seq != part.seq or idx != part.next:
                    st.broken += 1
                    del open_fp[key]
                    continue
                part.buf += f.data[1:]
                part.next += 1
                if len(part.buf) >= part.total:
                    message = bytes(part.buf[: part.total])
                    del open_fp[key]
            if message is not None:
                st.whole += 1
        else:
            message = f.data
        if message is None:
            continue
        if f.pgn == 60928 and len(message) >= 8:
            name = Name.from_bytes(message)
            dev.name = name
            a.claims.append((f.t, f.src, name))
        elif f.pgn == 126996 and len(message) >= 36:
            model = message[4:36].split(b"\xff")[0].split(b"\x00")[0].decode("ascii", "replace")
            dev.model = model.rstrip(" @")
        elif f.pgn == 59904 and len(message) >= 3:
            a.requests.append((f.t, f.src, f.dst, int.from_bytes(message[:3], "little")))
    return a


@dataclass
class _Partial:
    seq: int
    next: int
    total: int
    buf: bytearray


def report(a: Analysis, terminal_src: int | None = None) -> str:
    out: list[str] = []
    secs = a.seconds or 1.0
    out.append(f"{a.frames} frames over {a.seconds:.1f} s ({a.frames / secs:.0f}/s), "
               f"{len(a.devices)} devices, {len(a.pgns)} PGN/source pairs")
    if a.repeats:
        out.append(f"REPEATS: {a.repeats} frames identical to the one before within 2 ms "
                   "(a transmitter not getting ACKs: wiring or termination)")
    out.append("")
    out.append("Devices")
    for src in sorted(a.devices):
        d = a.devices[src]
        who = d.name.describe() if d.name else "no address claim seen"
        model = f" — {d.model}" if d.model else ""
        out.append(f"  {src:3d}  {who}{model}  ({d.frames} frames, {len(d.pgns)} PGNs)")
    out.append("")
    out.append("PGNs")
    for (pgn, src), st in sorted(a.pgns.items()):
        span = max(st.last_t - st.first_t, 0.0)
        count = st.whole if pgn in FAST_PACKET else st.frames
        rate = count / secs
        fp = ""
        if pgn in FAST_PACKET:
            fp = f"  fast packet {st.whole}/{st.starts} whole"
            if st.broken:
                fp += f", {st.broken} broken"
        shown = f"{rate:6.1f}/s" if rate >= 0.1 else f"{rate * 60:5.1f}/min"
        out.append(f"  {pgn:6d} {PGN_NAMES.get(pgn, ''):16s} from {src:3d}  {shown}{fp}"
                   f"  (seen {span:.0f} s)")
    if a.claims:
        out.append("")
        out.append("Address claims")
        for t, src, name in a.claims:
            out.append(f"  {t:10.3f}  {src:3d}  {name.describe()}")
    if a.requests:
        out.append("")
        out.append("ISO requests")
        for t, src, dst, pgn in a.requests[:40]:
            out.append(f"  {t:10.3f}  {src:3d} -> {dst:3d}  for {pgn} {PGN_NAMES.get(pgn, '')}")
        if len(a.requests) > 40:
            out.append(f"  … {len(a.requests) - 40} more")
    if terminal_src is not None and terminal_src in a.devices:
        bad = sorted(p for p in a.devices[terminal_src].pgns if p not in MANAGEMENT)
        out.append("")
        if bad:
            out.append(f"TERMINAL ({terminal_src}) SENT NON-MANAGEMENT PGNs: {bad}")
        else:
            out.append(f"Terminal ({terminal_src}) sent only network-management PGNs.")
    return "\n".join(out)


def per_source_rates(a: Analysis) -> dict[int, dict[int, float]]:
    rates: dict[int, dict[int, float]] = defaultdict(dict)
    secs = a.seconds or 1.0
    for (pgn, src), st in a.pgns.items():
        rates[src][pgn] = (st.whole if pgn in FAST_PACKET else st.frames) / secs
    return dict(rates)

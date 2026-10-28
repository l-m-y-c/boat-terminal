"""Readable summaries of the terminal's diagnostics.

/n2k.json (GET over HTTP, or the JSON line `NMEA DIAG` prints) and
/status.json. Field names are the contract with firmware/src/n2k_diag.cpp
and firmware/src/terminal_stats.cpp.
"""

from __future__ import annotations

import json
from typing import Any

from .n2k import MAKERS, PGN_NAMES

_NEVER = 0xFFFFFFFF


def find_json(lines: list[str], prefix: str = '{"v":1') -> dict[str, Any] | None:
    """The first JSON object in console output that starts with prefix."""
    for line in lines:
        start = line.find(prefix)
        if start < 0:
            continue
        try:
            return json.loads(line[start:])
        except json.JSONDecodeError:
            continue
    return None


def _age(ms: Any) -> str:
    if not isinstance(ms, int) or ms >= _NEVER:
        return "never"
    if ms < 1000:
        return "now"
    if ms < 60000:
        return f"{ms // 1000} s ago"
    return f"{ms // 60000} min ago"


def summarise_n2k(d: dict[str, Any]) -> str:
    out: list[str] = []
    bus = d.get("bus", {})
    self_ = d.get("self", {})
    hint = bus.get("hint", "ok")
    out.append(f"Source {bus.get('src', '?')}, bus {bus.get('state', '?')}, "
               f"{bus.get('fps', 0)} frames/s, {bus.get('mps', 0)} messages/s")
    node = self_.get("node", "off")
    if node == "claimed":
        out.append(f"This terminal: NMEA 2000 node, address {self_.get('address')}, "
                   f"NAME unique {self_.get('unique')}, maker code {self_.get('mfr')}, "
                   f"sent {self_.get('tx_frames', 0)} frames ({self_.get('tx_failed', 0)} refused, "
                   f"{self_.get('tx_pending', 0)} waiting for an ACK), "
                   f"bus-off {self_.get('bus_off', 0)}")
    else:
        out.append(f"This terminal: {node.replace('_', ' ')}")
    if hint != "ok":
        out.append(f"HINT {hint}: {bus.get('hint_text', '')}")
    lost = bus.get("lost_q", 0) + bus.get("lost_drv", 0) + bus.get("lost_hw", 0)
    out.append(f"Frames {bus.get('frames', 0)}, lost {lost} (queue {bus.get('lost_q', 0)} / "
               f"driver {bus.get('lost_drv', 0)} / controller {bus.get('lost_hw', 0)}), "
               f"lost messages {self_.get('lost_msgs', 0)}, bus errors {bus.get('bus_err', 0)}, "
               f"REC {bus.get('rec', 0)}, repeats {bus.get('repeats', 0)}, "
               f"fast packets incomplete {bus.get('fast_incomplete', 0)}, bad values {bus.get('bad_values', 0)}, "
               f"queue peak {bus.get('q_peak', 0)}/{bus.get('q_cap', 0)}, parse peak {bus.get('parse_peak_us', 0)} µs")

    devices = d.get("devices", [])
    out.append("")
    out.append(f"Devices ({len(devices)})")
    for dev in devices:
        maker = dev.get("maker") or (MAKERS.get(dev["mfr"]) if "mfr" in dev else None)
        who = maker or (f"maker {dev['mfr']}" if "mfr" in dev else "no address claim seen")
        extra = []
        if dev.get("class_name"):
            extra.append(dev["class_name"])
        if dev.get("model"):
            extra.append(dev["model"])
        out.append(f"  {dev.get('src', '?'):>3}  {who}{' — ' + ', '.join(extra) if extra else ''}"
                   f"  ({dev.get('pgns', 0)} PGNs, {dev.get('frames', 0)} frames, last {_age(dev.get('age_ms'))})")

    owners = d.get("sources", [])
    if owners:
        out.append("")
        out.append("Values and the device they come from")
        for o in owners:
            also = f", {o['other']} also sends it" if "other" in o else ""
            out.append(f"  {o.get('what', '?'):14s} from {o.get('src', '?'):>3}, last {_age(o.get('age_ms'))}{also}")

    conv = d.get("conv", [])
    if conv:
        out.append("")
        out.append("Converter (rx = used + rate-limited + n/a + bad + other device)")
        for c in conv:
            flag = "  <-- BAD VALUES" if c.get("bad", 0) else ""
            out.append(f"  {c['pgn']:6d} {PGN_NAMES.get(c['pgn'], ''):16s} rx {c.get('rx', 0):7d}  "
                       f"used {c.get('used', 0):7d}  limited {c.get('throttled', 0):7d}  "
                       f"n/a {c.get('na', 0):5d}  bad {c.get('bad', 0):5d}  other {c.get('other_src', 0):5d}{flag}")

    pgns = d.get("pgns", [])
    if pgns:
        out.append("")
        out.append("PGNs heard")
        for p in sorted(pgns, key=lambda r: (r.get("pgn", 0), r.get("src", 0))):
            fp = ""
            if p.get("fast"):
                starts, msgs = p.get("starts", 0), p.get("msgs", 0)
                fp = f"  fast packet {msgs}/{starts} whole"
                if starts and (starts - msgs) * 10 >= starts and starts - msgs > 0:
                    fp += "  <-- INCOMPLETE"
            out.append(f"  {p.get('pgn', 0):6d} {p.get('name', ''):16s} from {p.get('src', 0):>3}  "
                       f"{p.get('rate', 0):5.1f}/s  {p.get('msgs', 0)} msgs{fp}  last {_age(p.get('age_ms'))}")
    return "\n".join(out)


def summarise_status(d: dict[str, Any]) -> str:
    data = d.get("data", {}) if isinstance(d.get("data"), dict) else {}
    out = [
        f"Boat {d.get('name', '?')} ({d.get('boat', '?')}), sailboat id {d.get('sailboat_id', '?')}",
        f"Chart-app TCP clients {d.get('tcp', 0)}, NMEA lines good {d.get('nmea_good', 0)} / bad {d.get('nmea_bad', 0)}, "
        f"phone GNSS (LOCAL) {'armed' if d.get('inject_local') else 'off'}",
    ]
    if data:
        addr = data.get("addr", -1)
        out.append(f"Source {data.get('src', '?')}, {data.get('state', '?')}, {data.get('fps', 0)} frames/s, "
                   f"lost {data.get('lost', 0)}, bus errors {data.get('bus_err', 0)}, "
                   f"address {addr if isinstance(addr, int) and addr >= 0 else 'none'}, hint {data.get('hint', 'ok')}, "
                   f"up {data.get('up_s', 0)} s")
        out.append(f"Sentences out {data.get('out', 0)}, not delivered {data.get('out_lost', 0)} TCP / "
                   f"{data.get('udp_lost', 0)} UDP, apps {data.get('apps', 0)}")
    return "\n".join(out)


def summarise_network(d: dict[str, Any]) -> str:
    """GET /n2k/network.json or NMEA NET: every device, one line each, then issues."""
    devs = d.get("devices", [])
    disc = d.get("discovery", {})
    total = d.get("len_total", 0)
    out = [f"{len(devs)} devices, bus load {total} LEN ({total * 0.05:.2f} A), "
           f"discovery: {disc.get('requests', 0)} requests, {disc.get('answers', 0)} answers, "
           f"still asking {disc.get('asking', 0)}", ""]

    def order(x: dict[str, Any]) -> tuple[int, int, int]:
        return (0 if x.get("self") else 1, x.get("class", 999), x.get("address", 0))

    for x in sorted(devs, key=order):
        flags = ", ".join(i.get("code", "?") for i in x.get("issues", []))
        out.append(f"  {x.get('address', '?'):>3}  {x.get('title', ''):30s} {x.get('role', ''):22s} "
                   f"LEN {x.get('len', '?'):>2}  {x.get('install1', '')[:28]:28s}"
                   f"{'  complete' if x.get('complete') else '  incomplete'}{'  ISSUES: ' + flags if flags else ''}")
    problems = [(x, i) for x in devs for i in x.get("issues", [])]
    if problems:
        out += ["", "Needs a look"]
        for x, i in problems:
            out.append(f"  {x.get('address')} {x.get('title')}: {i.get('text')}")
    return "\n".join(out)


def summarise_device(d: dict[str, Any]) -> str:
    """GET /n2k/device.json?src=N or NMEA NET N."""
    dev = d.get("device", {})
    out = [f"{dev.get('title', '?')}, address {dev.get('address', '?')}, {dev.get('role', '')}"]
    for key, label in (("maker", "Maker"), ("mfr", "Maker code"), ("instance", "Instance"), ("unique", "Unique"),
                       ("model", "Model"), ("software", "Software"), ("model_version", "Model version"),
                       ("serial", "Serial"), ("len", "LEN"), ("n2k_version", "NMEA 2000 version"),
                       ("certification", "Certification"), ("install1", "Installed"), ("install2", ""),
                       ("heartbeat_ms", "Heartbeat ms"), ("address_changes", "Address changes")):
        if dev.get(key) not in (None, ""):
            out.append(f"  {label:18s} {dev[key]}")
    if dev.get("transmits"):
        out.append(f"  {'Transmit list':18s} {', '.join(str(p) for p in dev['transmits'])}")
    out.append(f"  {'Terminal uses it':18s} {', '.join(d.get('owns', [])) or 'nothing'}")
    for i in dev.get("issues", []):
        out.append(f"  ISSUE {i.get('code')}: {i.get('text')}")
    out += ["", "What it sends"]
    for p in d.get("pgns", []):
        out.append(f"  {p.get('pgn', 0):6d} {p.get('name', ''):16s} {p.get('rate', 0):5.1f}/s  "
                   f"{p.get('text') or p.get('hex', '')}")
    out += ["", "Recent messages"]
    for r in d.get("recent", [])[:20]:
        out.append(f"  {r.get('age_ms', 0) / 1000:6.1f} s  {r.get('pgn', 0):6d}  {r.get('text') or r.get('hex', '')}")
    return "\n".join(out)


def status_line(d: dict[str, Any]) -> str:
    """One line per NMEA DIAG snapshot: rate, devices, node, every error counter."""
    bus, me = d.get("bus", {}), d.get("self", {})
    lost = bus.get("lost_q", 0) + bus.get("lost_drv", 0) + bus.get("lost_hw", 0)
    return (f"{bus.get('state')} {bus.get('fps', 0)} f/s, {len(d.get('devices', []))} devices, "
            f"{len(d.get('pgns', []))} PGN/src, node {me.get('node')} {me.get('address', '')}, "
            f"lost {lost}, bus errors {bus.get('bus_err', 0)} REC {bus.get('rec', 0)}, "
            f"bus-off {me.get('bus_off', 0)}, tx refused {me.get('tx_failed', 0)}, "
            f"repeats {bus.get('repeats', 0)}, bad {bus.get('bad_values', 0)}, hint {bus.get('hint')}")


def changes(prev: dict[str, Any] | None, cur: dict[str, Any]) -> list[str]:
    """What is new since the last snapshot: devices, PGN/source pairs, an
    address change, bus-off, error bursts, a duplicate source, a reboot."""
    out: list[str] = []
    devs = {x.get("src"): x for x in cur.get("devices", [])}
    pgns = {(p.get("pgn"), p.get("src")): p for p in cur.get("pgns", [])}
    me, bus = cur.get("self", {}), cur.get("bus", {})
    if prev is None:
        for s, x in sorted(devs.items(), key=lambda kv: kv[0] or 0):
            out.append(f"device {s}: {x.get('maker') or x.get('mfr', '?')} {x.get('model', '')}".rstrip())
    else:
        pdevs = {x.get("src") for x in prev.get("devices", [])}
        ppgns = {(p.get("pgn"), p.get("src")) for p in prev.get("pgns", [])}
        pme, pbus = prev.get("self", {}), prev.get("bus", {})
        for s in sorted(set(devs) - pdevs, key=lambda v: v or 0):
            x = devs[s]
            out.append(f"NEW device {s}: {x.get('maker') or x.get('mfr', '?')} {x.get('model', '')}".rstrip())
        for s in sorted(pdevs - set(devs), key=lambda v: v or 0):
            out.append(f"device {s} gone")
        for k in sorted(set(pgns) - ppgns, key=lambda k: (k[0] or 0, k[1] or 0)):
            out.append(f"NEW PGN {k[0]} {pgns[k].get('name', '')} from {k[1]}")
        if me.get("address") != pme.get("address"):
            out.append(f"OUR ADDRESS {pme.get('address')} -> {me.get('address')}")
        if me.get("bus_off", 0) > pme.get("bus_off", 0):
            out.append(f"BUS-OFF x{me.get('bus_off', 0) - pme.get('bus_off', 0)} (total {me.get('bus_off')})")
        elif me.get("bus_off", 0) < pme.get("bus_off", 0) or bus.get("frames", 0) < pbus.get("frames", 0):
            out.append("counters went back to zero: the terminal rebooted")
        if bus.get("bus_err", 0) > pbus.get("bus_err", 0) and bus.get("frames", 0) >= pbus.get("frames", 0):
            out.append(f"bus errors +{bus.get('bus_err', 0) - pbus.get('bus_err', 0)}")
    for o in cur.get("sources", []):
        if "other" in o:
            out.append(f"DUPLICATE {o.get('what')}: {o.get('src')} and {o.get('other')} both send it")
    return out

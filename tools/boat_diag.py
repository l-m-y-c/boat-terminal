#!/usr/bin/env python3
"""One-command bench pull: everything the terminal and the phone kept from a
boat session, into sessions/<YYYY-MM-DD_HHMMSS>/ (gitignored).

    python3 tools/boat_diag.py                  # or: make boat-diag
    python3 tools/boat_diag.py --host 192.168.4.1 --out sessions/boat-2026-10-28/pull

Each part is tried on its own and a missing one is noted, not fatal:
  terminal/  over the UART (CH343): NMEA STATS, DIAG json, NET json, NMEA OUT ring
  wifi/      over the hotspot: terminal.py fetch-diag (bus ring incl. own TX,
             n2k.json, network.json, 0183 ring, 10 s of TCP 10110)
  phone/     over adb: relay-log.txt (the bridge diary), logcat (LmycRelay,
             flutter, WifiNetworkFactory, ConnectivityService), dumpsys
             connectivity, app version
  analysis/  capture.py summary + replay of the bus ring, sentence counts
Then INDEX.txt lists what is there.

Pull BEFORE powering the terminal off: its rings are in PSRAM.
"""
from __future__ import annotations

import argparse
import datetime as dt
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
PKG = "ca.lmyc.lmyc_app"
PY = sys.executable


def run(cmd: list[str], out: Path | None, timeout: float, notes: list[str], what: str) -> bool:
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=ROOT)
    except (subprocess.TimeoutExpired, FileNotFoundError) as exc:
        notes.append(f"{what}: {exc}")
        return False
    if out is not None:
        out.write_text(r.stdout + (("\n# stderr\n" + r.stderr) if r.stderr.strip() else ""), encoding="utf-8")
    if r.returncode != 0:
        notes.append(f"{what}: exit {r.returncode} {r.stderr.strip().splitlines()[-1:] or ''}")
        return False
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", help="folder (default sessions/<now>)")
    ap.add_argument("--host", default="192.168.4.1", help="terminal HTTP over the hotspot")
    ap.add_argument("--no-uart", action="store_true")
    ap.add_argument("--no-wifi", action="store_true")
    ap.add_argument("--no-phone", action="store_true")
    a = ap.parse_args()
    out = Path(a.out) if a.out else ROOT / "sessions" / dt.datetime.now().strftime("%Y-%m-%d_%H%M%S")
    notes: list[str] = []
    for d in ("terminal", "wifi", "phone", "analysis"):
        (out / d).mkdir(parents=True, exist_ok=True)
    con = [PY, str(HERE / "console.py")]
    term = [PY, str(HERE / "terminal.py"), "--host", a.host]

    if not a.no_uart:
        print("terminal over the UART ...")
        run(con + ["send", "NMEA", "STATS"], out / "terminal/stats.txt", 20, notes, "UART stats")
        run(con + ["diag", "--out", str(out / "terminal/diag.json")], out / "terminal/diag.txt", 30, notes, "UART diag")
        run(con + ["net", "--raw", "--out", str(out / "terminal/net.json")], out / "terminal/net.txt", 30, notes,
            "UART net")
        run(con + ["dump", "--out", str(out / "terminal/nmea-ring.txt")], out / "terminal/dump.txt", 150, notes,
            "UART NMEA OUT")

    if not a.no_wifi:
        print(f"terminal over Wi-Fi ({a.host}) ...")
        run(term + ["fetch-diag", "--out", str(out / "wifi")], out / "wifi/fetch.txt", 240, notes, "Wi-Fi fetch-diag")

    if not a.no_phone:
        print("phone over adb ...")
        ph = out / "phone"
        if run(["adb", "get-state"], None, 10, notes, "adb (phone plugged in, USB debugging allowed?)"):
            run(["adb", "pull", f"/sdcard/Android/data/{PKG}/files/", str(ph / "files")], ph / "pull.txt", 60, notes,
                "relay-log pull")
            run(["adb", "logcat", "-d", "-v", "threadtime"], ph / "logcat-all.txt", 60, notes, "logcat")
            run(["adb", "logcat", "-d", "-v", "threadtime", "-s", "LmycRelay:*", "flutter:*",
                 "WifiNetworkFactory:*", "ConnectivityService:*", "WifiClientModeImpl:*"],
                ph / "logcat-relay.txt", 60, notes, "logcat (relay)")
            run(["adb", "shell", "dumpsys", "connectivity"], ph / "dumpsys-connectivity.txt", 60, notes, "dumpsys")
            run(["adb", "shell", "dumpsys", "package", PKG], ph / "package.txt", 30, notes, "package")

    ring = out / "wifi/bus-ring.log"
    if ring.exists() and ring.stat().st_size > 200:
        print("analysis ...")
        run([PY, str(HERE / "capture.py"), "summary", str(ring)], out / "analysis/summary.txt", 120, notes, "summary")
        run([PY, str(HERE / "capture.py"), "replay", str(ring), "--sentences", str(out / "analysis/replay-sentences.txt"),
             "--json", str(out / "analysis/replay-n2k.json")], out / "analysis/replay.txt", 300, notes, "replay")

    index = [f"boat-diag pull {dt.datetime.now().isoformat(timespec='seconds')}", ""]
    for f in sorted(out.rglob("*")):
        if f.is_file():
            index.append(f"{f.stat().st_size:>10}  {f.relative_to(out)}")
    index += ["", "Missing or failed:"] + ([f"  {n}" for n in notes] or ["  nothing"])
    (out / "INDEX.txt").write_text("\n".join(index) + "\n", encoding="utf-8")
    print("\n".join(index))
    print(f"\nSaved {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

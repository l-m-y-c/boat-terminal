"""Tests for the developer tools. Standard library only:

    python3 -m unittest discover -s tools/tests -t tools
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))

from lmyc_dev import ROOT  # noqa: E402
from lmyc_dev.board import UsbipdDevice  # noqa: E402
from lmyc_dev.diag import find_json, summarise_n2k, summarise_status  # noqa: E402
from lmyc_dev.n2k import (  # noqa: E402
    Frame, Name, analyse, can_id, format_line, parse_line, pgn_of, report,
)

FIXTURE = TOOLS / "tests" / "fixtures" / "n2k_replay.json"


def fast_frames(t: float, pgn: int, src: int, payload: bytes, seq: int = 0) -> list[Frame]:
    """A message split into fast-packet frames, the way devices send it."""
    cid = can_id(6, pgn, src)
    frames = [Frame(t, cid, bytes([(seq << 5), len(payload)]) + payload[:6])]
    rest = payload[6:]
    idx = 1
    while rest:
        chunk, rest = rest[:7], rest[7:]
        frames.append(Frame(t + idx * 0.0005, cid, bytes([(seq << 5) | idx]) + chunk.ljust(7, b"\xff")))
        idx += 1
    return frames


def garmin_name() -> bytes:
    raw = (12345 | (229 << 21) | (130 << 40) | (120 << 49) | (4 << 60) | (1 << 63))
    return raw.to_bytes(8, "little")


_IOREG = """
+-o USB JTAG_serial debug unit@11332000  <class IOUSBHostDevice, id 0x1>
  | |   "idProduct" = 4097
  | |   "idVendor" = 12346
  | |   "USB Serial Number" = "58:E6:C5:72:D6:48"
  | +-o AppleUSBACMData  <class AppleUSBACMData, id 0x2>
  |   |   "idProduct" = 4097
  |   |   "idVendor" = 12346
  |   +-o IOSerialBSDClient  <class IOSerialBSDClient, id 0x3>
  |         "IOCalloutDevice" = "/dev/cu.usbmodem1133201"
+-o USB Single Serial@11340000  <class IOUSBHostDevice, id 0x4>
  | |   "idProduct" = 21971
  | |   "idVendor" = 6790
  | |   "USB Serial Number" = "5C37249341"
  |   +-o IOSerialBSDClient  <class IOSerialBSDClient, id 0x5>
  |         "IOCalloutDevice" = "/dev/cu.usbmodem5C372493411"
"""


class MacPorts(unittest.TestCase):
    def test_jtag_port_named_by_location(self) -> None:
        from lmyc_dev.ports import JTAG_VENDOR, CH343_VENDOR, parse_mac_callouts

        got = parse_mac_callouts(_IOREG)
        self.assertEqual(got["/dev/cu.usbmodem1133201"][:2], (JTAG_VENDOR, 0x1001))
        self.assertEqual(got["/dev/cu.usbmodem5C372493411"], (CH343_VENDOR, 0x55D3, "5C37249341"))


class CanIds(unittest.TestCase):
    def test_pdu2_and_pdu1(self) -> None:
        cid = can_id(3, 128267, 12)
        self.assertEqual(pgn_of(cid), 128267)
        self.assertEqual(cid & 0xFF, 12)
        # PDU1: destination in the low byte of the PGN field.
        req = can_id(6, 59904, 10, 35)
        self.assertEqual(pgn_of(req), 59904)
        self.assertEqual(Frame(0, req, b"").dst, 35)

    def test_candump_round_trip(self) -> None:
        line = "(12.345678) can0 09F80123#0102030405060708"
        f = parse_line(line)
        self.assertIsNotNone(f)
        assert f is not None
        self.assertEqual(f.pgn, 129025)
        self.assertEqual(f.src, 0x23)
        self.assertEqual(format_line(f), line)
        self.assertIsNone(parse_line("# a note"))
        self.assertIsNone(parse_line("(1.0) can0 123#ABC"))  # odd hex


class Names(unittest.TestCase):
    def test_garmin_display(self) -> None:
        n = Name.from_bytes(garmin_name())
        self.assertEqual(n.manufacturer, 229)
        self.assertEqual(n.device_class, 120)
        self.assertEqual(n.function, 130)
        self.assertEqual(n.unique, 12345)
        self.assertTrue(n.arbitrary)
        self.assertIn("Garmin", n.describe())
        self.assertIn("Display", n.describe())


class Capture(unittest.TestCase):
    def test_devices_fast_packets_and_repeats(self) -> None:
        frames = [Frame(0.0, can_id(6, 60928, 35), garmin_name())]
        product = (2101).to_bytes(2, "little") + (1234).to_bytes(2, "little") + b"GPSMAP 740s".ljust(32, b"\xff")
        product += b"\xff" * (134 - len(product))
        frames += fast_frames(0.1, 126996, 35, product)
        gnss = bytes(range(43))
        frames += fast_frames(1.0, 129029, 35, gnss, seq=1)
        broken = fast_frames(2.0, 129029, 35, gnss, seq=2)
        del broken[3]  # a frame lost on the wire
        frames += broken
        frames += fast_frames(3.0, 129029, 35, gnss, seq=3)
        same = Frame(4.0, can_id(2, 127250, 8), b"\x01\x02\x03\x04\x05\x06\x07\x08")
        frames += [same, Frame(4.0006, same.can_id, same.data)]
        frames += [Frame(5.0, can_id(6, 59904, 35, 23), (126996).to_bytes(3, "little"))]
        a = analyse(frames)
        self.assertEqual(a.devices[35].model, "GPSMAP 740s")
        self.assertEqual(a.devices[35].name.manufacturer, 229)  # type: ignore[union-attr]
        st = a.pgns[(129029, 35)]
        self.assertEqual((st.starts, st.whole, st.broken), (3, 2, 1))
        self.assertEqual(a.repeats, 1)
        self.assertEqual(a.requests[0][1:], (35, 23, 126996))
        text = report(a)
        self.assertIn("GPSMAP 740s", text)
        self.assertIn("fast packet 2/3 whole, 1 broken", text)
        self.assertIn("REPEATS", text)

    def test_terminal_only_sends_management(self) -> None:
        ok = [Frame(0.0, can_id(6, 60928, 23), garmin_name()), Frame(60.0, can_id(7, 126993, 23), b"\x60\xea\x00\x00\xff\xff\xff\xff")]
        self.assertIn("only network-management", report(analyse(ok), terminal_src=23))
        bad = ok + [Frame(61.0, can_id(3, 128267, 23), b"\x01" * 8)]
        self.assertIn("SENT NON-MANAGEMENT PGNs: [128267]", report(analyse(bad), terminal_src=23))


class Diagnostics(unittest.TestCase):
    def test_summary_of_replay_json(self) -> None:
        d = json.loads(FIXTURE.read_text())
        text = summarise_n2k(d)
        self.assertIn("Devices (6)", text)
        self.assertIn("Values and the device they come from", text)
        self.assertIn("128267 Depth", text)
        self.assertNotIn("HINT", text)

    def test_hint_and_node(self) -> None:
        d = {"bus": {"src": "n2k", "state": "live", "hint": "repeats", "hint_text": "The same frame…"},
             "self": {"node": "claimed", "address": 23, "unique": 7, "mfr": 2046}}
        text = summarise_n2k(d)
        self.assertIn("HINT repeats", text)
        self.assertIn("address 23", text)

    def test_find_json_in_console_output(self) -> None:
        lines = ["NMEA ok, node: NMEA 2000 node, address 23", 'junk {"v":1,"bus":{"src":"sim"}}', "x"]
        self.assertEqual(find_json(lines), {"v": 1, "bus": {"src": "sim"}})
        self.assertIsNone(find_json(["nothing"]))

    def test_status(self) -> None:
        text = summarise_status({"boat": "BENCH-01", "name": "Bench", "tcp": 1,
                                 "data": {"src": "n2k", "state": "live", "addr": 23, "hint": "ok"}})
        self.assertIn("address 23", text)


class Usbipd(unittest.TestCase):
    def test_parse(self) -> None:
        from lmyc_dev import board
        sample = ("Connected:\nBUSID  VID:PID    DEVICE                                                        STATE\n"
                  "1-5    303a:1001  USB Serial Device (COM34), USB JTAG/serial debug unit         Attached\n"
                  "1-7    27c6:6594  Goodix MOC Fingerprint                                        Not shared\n"
                  "\nPersisted:\nGUID  DEVICE\n")
        orig_path, orig_check = board.usbipd_path, subprocess.run
        board.usbipd_path = lambda: "usbipd.exe"  # type: ignore[assignment]
        subprocess.run = lambda *a, **k: subprocess.CompletedProcess(a, 0, sample, "")  # type: ignore[assignment]
        try:
            devs = board.terminal_usbipd()
        finally:
            board.usbipd_path = orig_path  # type: ignore[assignment]
            subprocess.run = orig_check  # type: ignore[assignment]
        self.assertEqual(devs, [UsbipdDevice("1-5", "303a:1001",
                                             "USB Serial Device (COM34), USB JTAG/serial debug unit", "Attached")])


def _have_library() -> bool:
    return any((ROOT / "firmware" / ".pio" / "libdeps").glob("*/NMEA2000-library/src")) and \
        shutil.which(os.environ.get("CXX", "c++")) is not None


@unittest.skipUnless(_have_library(), "needs `make firmware` (NMEA2000 library) and a C++ compiler")
class Replay(unittest.TestCase):
    def test_simulator_capture_round_trip(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            cap = os.path.join(tmp, "sim.log")
            out = os.path.join(tmp, "out.json")
            script = str(ROOT / "firmware" / "test_host" / "replay.sh")
            env = dict(os.environ, TMPDIR=tmp)
            subprocess.run(["sh", script, "--sim", "30", "--write", cap], check=True, env=env,
                           capture_output=True)
            r = subprocess.run(["sh", script, cap, "--json", out], check=True, env=env,
                               capture_output=True, text=True)
            self.assertIn("0 never completed", r.stdout)
            self.assertIn("$SDDPT", r.stdout)
            d = json.loads(Path(out).read_text())
            self.assertEqual(len(d["devices"]), 6)
            self.assertEqual(d["bus"]["bad_values"], 0)



from lmyc_dev import board as _board  # noqa: E402
from lmyc_dev.diag import changes, status_line  # noqa: E402
from lmyc_dev.ports import Port  # noqa: E402


def _sock(kind: str, answered: bool, mode: str | None) -> "_board.Socket":
    port = Port(f"/dev/{kind}", 0x1A86 if kind == "uart" else 0x303A, 0, "x", kind)
    return _board.Socket(port, answered, mode, "")


class ModeAware(unittest.TestCase):
    def test_parse_mode(self) -> None:
        self.assertEqual(_board.parse_mode(["NMEA Simulator frames=3"]), _board.SIM)
        self.assertEqual(_board.parse_mode(["junk", "NMEA NMEA 2000 bus=ok"]), _board.N2K)
        self.assertIsNone(_board.parse_mode(["N2K: started"]))

    def test_sim_log_on_usb_commands_on_uart(self) -> None:
        s = [_sock("uart", True, _board.SIM), _sock("usb", True, _board.SIM)]
        self.assertEqual(_board.pick(s, "log").port.kind, "usb")
        self.assertEqual(_board.pick(s, "command").port.kind, "uart")
        self.assertFalse(any("switch" in n for n in _board.explain(s)))

    def test_n2k_uart_only(self) -> None:
        s = [_sock("uart", True, _board.N2K)]
        self.assertEqual(_board.pick(s, "log").port.kind, "uart")
        notes = _board.explain(s)
        self.assertTrue(any("only the UART" in n for n in notes))
        self.assertFalse(any("switch" in n for n in notes))

    def test_switch_only_when_uart_gets_no_reply(self) -> None:
        s = [_sock("uart", False, None), _sock("usb", True, _board.SIM)]
        self.assertTrue(any("UART1" in n for n in _board.explain(s)))
        self.assertEqual(_board.pick(s, "command").port.kind, "usb")
        self.assertTrue(any("UART1" in n for n in _board.explain([_sock("uart", False, None)])))
        self.assertFalse(any("UART1" in n for n in _board.explain([_sock("usb", True, _board.SIM)])))
        self.assertIsNone(_board.pick([_sock("uart", False, None)], "log"))

    def test_flash_advice(self) -> None:
        self.assertIn("cannot flash", _board.flash_advice(_board.N2K, False))
        self.assertIn("USB socket", _board.flash_advice(None, False))
        self.assertIn("Simulator", _board.flash_advice(_board.SIM, True))

    def test_terminal_socket_note(self) -> None:
        import terminal
        self.assertIn("only the UART", terminal.socket_note("n2k"))
        self.assertIn("Simulator", terminal.socket_note("sim"))


class SessionChanges(unittest.TestCase):
    def snap(me, **kw):
        d = {"bus": {"frames": 100, "bus_err": 0, "fps": 50, "state": "ok", "hint": "ok"},
             "self": {"node": "claimed", "address": 23, "bus_off": 0},
             "devices": [{"src": 1, "maker": "Garmin", "model": "GPSMAP 740"}],
             "pgns": [{"pgn": 129025, "src": 1, "name": "position"}], "sources": []}
        for k, v in kw.items():
            d[k].update(v) if isinstance(v, dict) else d.__setitem__(k, v)
        return d

    def test_first_snapshot_lists_devices(self) -> None:
        self.assertEqual(changes(None, self.snap()), ["device 1: Garmin GPSMAP 740"])
        self.assertIn("bus-off 0", status_line(self.snap()))

    def test_events(self) -> None:
        a = self.snap()
        b = self.snap(self={"address": 24, "bus_off": 1}, bus={"frames": 200, "bus_err": 5},
                      devices=a["devices"] + [{"src": 2, "maker": "Garmin", "model": "GPSMAP 740"}],
                      pgns=a["pgns"] + [{"pgn": 130306, "src": 2, "name": "wind"}],
                      sources=[{"what": "heading", "src": 1, "other": 2}])
        got = "\n".join(changes(a, b))
        for want in ("NEW device 2", "NEW PGN 130306", "OUR ADDRESS 23 -> 24", "BUS-OFF x1",
                     "bus errors +5", "DUPLICATE heading"):
            self.assertIn(want, got)

    def test_reboot(self) -> None:
        a = self.snap()
        b = self.snap(bus={"frames": 5})
        self.assertTrue(any("rebooted" in c for c in changes(a, b)))


class FetchDiagTest(unittest.TestCase):
    """terminal.py fetch-diag drains the paged rings until more=0."""

    def test_drain_and_fetch(self):
        import http.server
        import threading
        import terminal

        frames = [f"({i}.000000) can0 09F80101#{i:02X}00000000000000" for i in range(1, 8)]
        sentences = ["1.000 S $GPRMC,x*00", "1.100 L tcp client 1 connected"]

        class H(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                from urllib.parse import urlparse, parse_qs
                u = urlparse(self.path)
                after = int(parse_qs(u.query).get("after", ["0"])[0])
                if u.path == "/n2k/frames":
                    start = max(after, 1)
                    chunk = frames[start - 1:start - 1 + 3]
                    nxt = start + len(chunk)
                    more = 1 if nxt <= len(frames) else 0
                    body = f"#next {nxt} lost 0 more {more}\n" + "\n".join(chunk)
                elif u.path == "/diag/nmea.txt":
                    body = f"#next {len(sentences)} lost 0 more 0\n" + "\n".join(sentences)
                else:
                    body = "{}"
                data = body.encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        srv = http.server.HTTPServer(("127.0.0.1", 0), H)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        host = f"127.0.0.1:{srv.server_port}"
        try:
            rows, lost, _ = terminal._drain(host, "/n2k/frames", 1)
            self.assertEqual(rows, frames)
            self.assertEqual(lost, 0)
            with tempfile.TemporaryDirectory() as d:
                r = subprocess.run([sys.executable, str(TOOLS / "terminal.py"), "--host", host, "fetch-diag",
                                    "--out", d, "--tcp-seconds", "0"], capture_output=True, text=True, timeout=60)
                self.assertEqual(r.returncode, 0, r.stderr)
                ring = (Path(d) / "bus-ring.log").read_text().splitlines()
                self.assertEqual([l for l in ring if not l.startswith("#")], frames)
                self.assertEqual((Path(d) / "nmea-ring.txt").read_text().splitlines(), sentences)
                self.assertTrue((Path(d) / "network.json").exists())
        finally:
            srv.shutdown()


class BoatDiagRunTest(unittest.TestCase):
    """boat_diag.run survives binary output (logcat has non-UTF-8 bytes)."""

    def test_non_utf8_output(self):
        import boat_diag
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "x.txt"
            notes: list[str] = []
            ok = boat_diag.run([sys.executable, "-c", "import sys; sys.stdout.buffer.write(b'a\\xc0b')"],
                               out, 20, notes, "bin")
            self.assertTrue(ok, notes)
            self.assertIn("a", out.read_text(encoding="utf-8"))


class AppIdTest(unittest.TestCase):
    """The Play applicationId is the one boat_diag pulls from."""

    def test_application_id(self):
        import re
        import boat_diag
        gradle = (TOOLS.parent / "mobile/android/app/build.gradle.kts").read_text()
        app_id = re.search(r'applicationId = "([^"]+)"', gradle).group(1)
        self.assertEqual(app_id, "ca.lmyc.boat")
        self.assertEqual(boat_diag.PKG, app_id)
        manifest = (TOOLS.parent / "mobile/android/app/src/main/AndroidManifest.xml").read_text()
        self.assertIn('android:label="LMYC Boat Terminal"', manifest)


if __name__ == "__main__":
    unittest.main()


class FakeTerminal(unittest.TestCase):
    """The console tools against a pseudo-terminal that answers like the firmware."""

    def setUp(self) -> None:
        import pty
        import threading

        self.master, slave = pty.openpty()
        self.path = os.ttyname(slave)
        self._slave = slave
        self._stop = False
        fixture = FIXTURE.read_text().strip()

        def serve() -> None:
            buf = b""
            while not self._stop:
                try:
                    import select
                    r, _, _ = select.select([self.master], [], [], 0.05)
                    if not r:
                        continue
                    buf += os.read(self.master, 1024)
                except OSError:
                    return
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    cmd = line.strip().decode()
                    if cmd == "NMEA STATS":
                        os.write(self.master, b"NMEA Simulator Simulated  frames 10 (1/s, peak 1)\r\n")
                    elif cmd == "NMEA DIAG":
                        os.write(self.master, b"NMEA ok, node: Not on a bus, address 254\r\n")
                        os.write(self.master, fixture.encode() + b"\r\n")

        self._thread = threading.Thread(target=serve, daemon=True)
        self._thread.start()

    def tearDown(self) -> None:
        self._stop = True
        self._thread.join(1)
        os.close(self.master)
        os.close(self._slave)

    def test_probe_sees_the_app(self) -> None:
        from lmyc_dev import board
        st = board.probe("any", self.path)
        self.assertEqual(st.state, "app")
        self.assertIn("Simulated", st.detail)

    def test_diag_reply_parses(self) -> None:
        from lmyc_dev.ports import Console
        with Console(self.path) as con:
            lines = con.command("NMEA DIAG", 1.0)
        self.assertTrue(any(l.startswith("NMEA ok") for l in lines))
        d = find_json(lines)
        self.assertIsNotNone(d)
        assert d is not None
        self.assertEqual(len(d["devices"]), 6)

    def test_session_saves_log_and_snapshots(self) -> None:
        import signal
        import time

        with tempfile.TemporaryDirectory() as tmp:
            p = subprocess.Popen([sys.executable, str(TOOLS / "console.py"), "--port", self.path, "session",
                                  "--every", "1", "--out", tmp], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 text=True)
            time.sleep(2.5)
            p.send_signal(signal.SIGINT)
            out, _ = p.communicate(timeout=10)
            self.assertIn("devices", out)
            self.assertIn("Last snapshot", out)
            snaps = Path(tmp, "diag.jsonl").read_text().splitlines()
            self.assertGreaterEqual(len(snaps), 2)
            self.assertIn("NMEA ok", Path(tmp, "console.log").read_text())


class Network(unittest.TestCase):
    """The topology summaries, from JSON captured on the board (NMEA NET)."""

    def test_network_summary(self) -> None:
        from lmyc_dev.diag import summarise_network
        d = json.loads((TOOLS / "tests" / "fixtures" / "n2k_network_sim.json").read_text())
        text = summarise_network(d)
        self.assertIn("7 devices, bus load 19 LEN (0.95 A)", text)
        self.assertIn("Sim depth / speed / temp", text)
        self.assertIn("Sim: masthead", text)
        self.assertNotIn("ISSUES", text)
        # The terminal first.
        self.assertLess(text.index("LMYC Boat Terminal"), text.index("Sim GPS"))

    def test_device_summary(self) -> None:
        from lmyc_dev.diag import summarise_device
        d = json.loads((TOOLS / "tests" / "fixtures" / "n2k_device_12_sim.json").read_text())
        text = summarise_device(d)
        self.assertIn("address 12", text)
        self.assertIn("depth", text)
        self.assertIn("Depth ", text)
        self.assertIn("Recent messages", text)


class BoatIdentity(unittest.TestCase):
    def setUp(self) -> None:
        import importlib.util
        spec = importlib.util.spec_from_file_location("boat_tool", TOOLS / "boat.py")
        self.boat = importlib.util.module_from_spec(spec)
        sys.modules["boat_tool"] = self.boat  # dataclasses look their module up
        assert spec.loader is not None
        spec.loader.exec_module(self.boat)

    def test_fleet_ids_follow_the_rules(self) -> None:
        for b in self.boat.FLEET:
            self.assertIsNone(self.boat.check_id(b.boat_id), b.boat_id)
        self.assertEqual(self.boat.find_boat("baca-lele").boat_id, "BACA-LELE")
        self.assertEqual(self.boat.find_boat("Teachers Pet").sailboat_id, 506)
        self.assertIsNone(self.boat.find_boat("titanic"))

    def test_bad_ids_and_names(self) -> None:
        self.assertIsNotNone(self.boat.check_id("baca lele"))
        self.assertIsNotNone(self.boat.check_id("A" * 17))
        self.assertIsNotNone(self.boat.check_name('Say "hi"'))

    def test_apply_replaces_or_adds(self) -> None:
        example = (TOOLS.parent / "firmware" / "src" / "secrets.h.example").read_text()
        out = self.boat.apply(example, "BACA-LELE", "Baca Lele", 276)
        s = self.boat.read_settings(out)
        self.assertEqual(s, {"LMYC_BOAT_ID": "BACA-LELE", "LMYC_BOAT_NAME": "Baca Lele", "LMYC_SAILBOAT_ID": "276"})
        self.assertIn('#define WIFI_SSID', out)  # nothing else touched
        bare = "#pragma once\n#define WIFI_SSID \"x\"\n"
        added = self.boat.read_settings(self.boat.apply(bare, "BENCH-01", "Bench", 0))
        self.assertEqual(added["LMYC_BOAT_ID"], "BENCH-01")

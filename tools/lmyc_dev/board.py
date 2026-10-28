"""The board itself: where it is, what it is running, and getting it back.

Lessons from the bench, built in:
  - On WSL every chip reset re-enumerates USB and usbipd drops the device
    unless it was attached with --auto-attach.
  - After `pio run -t upload` the RTC-WDT reset can leave the ESP32-S3 in
    the ROM bootloader, with a silent console. esptool with
    --before no_reset tells the two apart, and --after hard_reset boots it.
  - On NMEA 2000 the USB socket is off by design; only the UART socket works.
"""

from __future__ import annotations

import glob
import os
import re
import shutil
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

from . import FIRMWARE
from .ports import Console, Port, find_port, is_wsl, list_ports

USBIPD = "/mnt/c/Program Files/usbipd-win/usbipd.exe"
TERMINAL_VIDS = ("303a", "1a86")


# ---- WSL / usbipd ---------------------------------------------------------

@dataclass
class UsbipdDevice:
    busid: str
    vid_pid: str
    name: str
    state: str


def usbipd_path() -> str | None:
    found = shutil.which("usbipd.exe")
    if found:
        return found
    return USBIPD if os.path.exists(USBIPD) else None


# USB filter drivers usbipd warned about on the last list. CsDeviceControl is
# CrowdStrike Falcon device control, which can block devices by policy.
_filters: list[str] = []


def usbipd_list() -> list[UsbipdDevice]:
    exe = usbipd_path()
    if exe is None:
        return []
    try:
        p = subprocess.run([exe, "list"], capture_output=True, text=True, errors="replace", timeout=20)
    except (OSError, subprocess.SubprocessError):
        return []
    text = p.stdout
    _filters.clear()
    _filters.extend(re.findall(r"Unknown USB filter '([^']+)'", p.stderr))
    out: list[UsbipdDevice] = []
    in_connected = False
    for line in text.splitlines():
        if line.startswith("Connected:"):
            in_connected = True
            continue
        if line.startswith("Persisted:"):
            break
        m = re.match(r"^(\S+)\s+([0-9a-fA-F]{4}:[0-9a-fA-F]{4})\s+(.*?)\s{2,}(\S.*)$", line)
        if in_connected and m:
            out.append(UsbipdDevice(m.group(1), m.group(2).lower(), m.group(3).strip(), m.group(4).strip()))
    return out


def terminal_usbipd() -> list[UsbipdDevice]:
    return [d for d in usbipd_list() if d.vid_pid.split(":")[0] in TERMINAL_VIDS]


def attach(dev: UsbipdDevice, log_dir: Path) -> str:
    """Attach to WSL with --auto-attach, which stays running and re-attaches
    after every reset. It runs detached; its output goes to a log file."""
    exe = usbipd_path()
    if exe is None:
        return "usbipd-win is not installed on Windows."
    if dev.state.lower().startswith("not shared"):
        return (f"{dev.busid} is not shared yet. Once, in an administrator PowerShell:\n"
                f"    usbipd bind --busid {dev.busid}")
    if dev.state.lower().startswith("attached"):
        return f"{dev.busid} ({dev.name}) is already attached."
    log_dir.mkdir(parents=True, exist_ok=True)
    log = open(log_dir / f"usbipd-{dev.busid}.log", "ab")
    subprocess.Popen([exe, "attach", "--wsl", "--busid", dev.busid, "--auto-attach"],
                     stdout=log, stderr=log, stdin=subprocess.DEVNULL, start_new_session=True)
    return f"Attaching {dev.busid} ({dev.name}) with --auto-attach (log: {log.name})."


# ---- esptool ----------------------------------------------------------------

def _pio_home() -> Path:
    return Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))


def pio_cmd() -> str | None:
    found = shutil.which("pio")
    if found:
        return found
    cand = _pio_home() / "penv" / "bin" / "pio"
    return str(cand) if cand.exists() else None


def esptool_cmd() -> list[str] | None:
    home = _pio_home()
    py = home / "penv" / "bin" / "python"
    script = home / "packages" / "tool-esptoolpy" / "esptool.py"
    if py.exists() and script.exists():
        return [str(py), str(script)]
    found = shutil.which("esptool.py") or shutil.which("esptool")
    return [found] if found else None


def esptool(port: str, before: str, after: str, command: str, timeout: float = 30) -> tuple[bool, str]:
    cmd = esptool_cmd()
    if cmd is None:
        return False, "esptool not found (run `make firmware` once so PlatformIO fetches it)."
    try:
        p = subprocess.run(cmd + ["--port", port, "--before", before, "--after", after, command],
                           capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, "esptool timed out"
    return p.returncode == 0, (p.stdout + p.stderr)


# ---- what the board is doing --------------------------------------------------

@dataclass
class Status:
    state: str  # "app", "bootloader", "silent", "absent"
    port: Port | None
    detail: str


def _in_bootloader(port: Port) -> bool:
    """The ROM loader answers esptool on both sockets; no_reset leaves the chip alone."""
    ok, text = esptool(port.path, "no_reset", "no_reset", "chip_id", timeout=15)
    return ok and "Staying in bootloader" in text


# ---- mode-aware sockets ----------------------------------------------------
#
# Simulator: CAN is closed, so the native USB socket is up and carries the
# app log; commands work on either socket, and the UART mirrors the log
# (firmware 4c2c408 and later mirror it; older builds only answer commands
# there). NMEA 2000: GPIO19/20 are CAN, the USB socket is gone by design and
# only the UART socket works.

SIM = "Simulator"
N2K = "NMEA 2000"


def parse_mode(lines: list[str]) -> str | None:
    """The data source from an NMEA STATS reply: 'NMEA Simulator ...' or
    'NMEA NMEA 2000 ...'. None if no STATS line is there."""
    for line in lines:
        if line.startswith("NMEA Simulator"):
            return SIM
        if line.startswith("NMEA NMEA 2000"):
            return N2K
    return None


def ask_stats(path: str, tries: int = 3, seconds: float = 1.5) -> tuple[list[str], str | None]:
    """NMEA STATS, retried: the first line after the port opens can land on a
    half-received line and be dropped, so a blank line goes first."""
    with Console(path) as con:
        for _ in range(tries):
            con.send("")
            reply = con.command("NMEA STATS", seconds)
            stats = [l for l in reply if l.startswith("NMEA ")]
            if stats:
                return stats, parse_mode(stats)
    return [], None


@dataclass
class Socket:
    port: Port
    answered: bool
    mode: str | None
    line: str  # the STATS line, or why there is none


def survey(path: str | None = None) -> list[Socket]:
    """Every terminal socket that is plugged in, asked NMEA STATS. Opening
    either socket leaves DTR/RTS alone and never resets the chip."""
    ports = [find_port("any", path)] if path else [p for p in list_ports() if p.kind in ("uart", "usb")]
    out: list[Socket] = []
    for port in sorted(ports, key=lambda p: 0 if p.kind == "uart" else 1):
        try:
            stats, mode = ask_stats(port.path)
        except (OSError, ConnectionError) as exc:
            out.append(Socket(port, False, None, f"could not open: {exc}"))
            continue
        out.append(Socket(port, bool(stats), mode, stats[-1] if stats else "no reply to NMEA STATS"))
    return out


def board_mode(sockets: list[Socket]) -> str | None:
    for s in sockets:
        if s.mode:
            return s.mode
    return None


def explain(sockets: list[Socket]) -> list[str]:
    """What the sockets say, in words, and what to do about silence. The
    UART select switch is suspected only when the UART is plugged in and
    gets no reply to a command at all."""
    mode = board_mode(sockets)
    uart = next((s for s in sockets if s.port.kind == "uart"), None)
    usb = next((s for s in sockets if s.port.kind == "usb"), None)
    notes: list[str] = []
    if mode == SIM:
        notes.append("Simulator mode: the app log is on the USB socket (and mirrored to the UART on "
                     "current firmware); commands work on either socket.")
    elif mode == N2K:
        notes.append("NMEA 2000 mode: the USB socket's pins are CAN, so only the UART socket works.")
    if uart is None:
        if mode == SIM:
            notes.append("No UART cable: fine on the simulator. On NMEA 2000 you will need it.")
        elif usb is None:
            notes.append("No terminal socket at all: check the cable is a data cable. On NMEA 2000 "
                         "use the socket marked UART.")
    elif not uart.answered:
        if mode == SIM:
            notes.append("The UART is plugged in but did not answer while the USB socket did: set the "
                         "board's UART select switch to UART1 (on UART2 the Type-C socket is "
                         "disconnected).")
        elif mode is None:
            notes.append("The UART is plugged in and got no reply. Either the chip is in the ROM "
                         "bootloader, or the UART select switch is on UART2: set it to UART1.")
    if usb is not None and mode == N2K:
        notes.append("A USB socket is listed while the app reports NMEA 2000: unusual; unplug it.")
    if usb is not None and not usb.answered and mode is None and uart is None:
        notes.append("The USB socket did not answer: the app is not running (bootloader?), or it "
                     "has just switched to NMEA 2000 and is closing USB.")
    return notes


def pick(sockets: list[Socket], purpose: str) -> Socket | None:
    """purpose "log": where the full log is; "command": any socket that
    answers, UART first (it never resets the chip and survives NMEA 2000)."""
    live = [s for s in sockets if s.answered]
    if not live:
        return None
    if purpose == "log" and board_mode(sockets) == SIM:
        for s in live:
            if s.port.kind == "usb":
                return s
    for s in live:
        if s.port.kind == "uart":
            return s
    return live[0]


def probe(prefer: str = "any", path: str | None = None) -> Status:
    try:
        port = find_port(prefer, path)
    except LookupError as exc:
        return Status("absent", None, str(exc))
    try:
        stats, mode = ask_stats(port.path)
    except (OSError, ConnectionError) as exc:
        return Status("silent", port, f"could not read {port.path}: {exc}")
    if stats:
        return Status("app", port, stats[-1])
    # Silent: the ROM bootloader first (BOOT held at reset, or left there by
    # an upload), on this port and the board's other socket.
    others = [p for p in list_ports() if p.kind in ("uart", "usb") and p.path != port.path]
    for candidate in [port] + others:
        if _in_bootloader(candidate):
            return Status("bootloader", candidate, "ROM download mode: the app is not running.")
    detail = "Port open, but the app did not answer NMEA STATS."
    if port.kind == "uart":
        detail += (" Check the board's UART select switch is on UART1: on UART2 the Type-C socket "
                   "is disconnected and not even the boot banner comes through.")
    return Status("silent", port, detail)


def flash_advice(mode: str | None, have_usb: bool) -> str:
    if mode == N2K:
        return ("The terminal is in NMEA 2000 mode, so the USB socket is CAN and cannot flash. Either "
                "switch to Simulator (Data source screen, or `console.py send NMEA SIM`; unplug the "
                "NMEA 2000 drop first) and flash over USB, or hold BOOT, tap RESET, release BOOT with a "
                "cable in the USB socket.\n" + uart_flash_note())
    if not have_usb:
        return ("Flashing needs the USB socket. Plug a data cable into the socket marked USB "
                "(Simulator mode).\n" + uart_flash_note())
    return "Simulator mode: flashing over the USB socket."


def boot_app(port: Port) -> tuple[bool, str]:
    """From the bootloader into the app, without re-entering download mode."""
    if port.kind != "usb":
        return False, ("Only the USB socket can reset the chip. On the UART socket press RESET "
                       "on the board.")
    ok, text = esptool(port.path, "no_reset", "hard_reset", "read_mac")
    if not ok:
        ok, text = esptool(port.path, "default_reset", "hard_reset", "read_mac")
    return ok, text


def wait_for_port(prefer: str, seconds: float) -> Port | None:
    """A port that exists and that we may open: after a re-attach udev
    takes a moment to hand the new device node to the dialout group."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            port = find_port(prefer)
            if os.access(port.path, os.R_OK | os.W_OK):
                return port
        except LookupError:
            pass
        time.sleep(0.5)
    return None


def upload(port: Port) -> int:
    pio = pio_cmd()
    if pio is None:
        raise FileNotFoundError("PlatformIO (pio) not found.")
    return subprocess.call([pio, "run", "-t", "upload", "--upload-port", port.path], cwd=str(FIRMWARE))


def uart_flash_note() -> str:
    return ("Flashing over the UART socket: hold BOOT, tap RESET, release BOOT, then flash. "
            "It has no auto-reset. (docs/14: on a Mac the CH343 may not carry flash data; use the "
            "USB socket on the simulator.)")


def default_log_dir() -> Path:
    return Path(os.environ.get("LMYC_TOOLS_LOG", Path.home() / ".cache" / "lmyc-boat-terminal"))


def wsl_hint() -> str:
    if not is_wsl():
        return ""
    devs = terminal_usbipd()
    note = ""
    if "CsDeviceControl" in _filters:
        note = ("\nCrowdStrike device control (CsDeviceControl) is filtering USB on this PC. If a "
                "device never shows up, that is company policy: ask IT, do not work around it.")
    if not devs:
        return ("Windows does not see the terminal at all (usbipd list). Check the cable, and on "
                "NMEA 2000 use the UART socket: the USB socket is off by design." + note)
    return "\n".join(f"Windows sees {d.busid} {d.vid_pid} {d.name}: {d.state}" for d in devs) + note


def glob_logs(pattern: str) -> list[str]:
    return sorted(glob.glob(pattern))

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
from .ports import Console, Port, find_port, is_wsl

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


def probe(prefer: str = "any", path: str | None = None) -> Status:
    try:
        port = find_port(prefer, path)
    except LookupError as exc:
        return Status("absent", None, str(exc))
    try:
        with Console(port.path) as con:
            reply = con.command("NMEA STATS", 1.5)
    except (OSError, ConnectionError) as exc:
        return Status("silent", port, f"could not read {port.path}: {exc}")
    stats = [l for l in reply if l.startswith("NMEA ")]
    if stats:
        return Status("app", port, stats[-1])
    if port.kind == "usb":
        ok, text = esptool(port.path, "no_reset", "no_reset", "chip_id", timeout=15)
        if ok and "Staying in bootloader" in text:
            return Status("bootloader", port, "ROM download mode: the app is not running.")
    return Status("silent", port, "Port open, but the app did not answer NMEA STATS.")


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
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            return find_port(prefer)
        except LookupError:
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

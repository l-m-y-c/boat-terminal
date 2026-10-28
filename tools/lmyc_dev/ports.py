"""Find the terminal's two serial ports and open them without resetting it.

The board has two Type-C sockets:

  UART  CH343 USB-serial (VID 0x1A86) on UART0. Works in every mode, and
        opening it never resets the chip. The only log port on NMEA 2000.
  USB   the ESP32-S3's own USB Serial/JTAG (VID 0x303A). Gone on NMEA 2000,
        because GPIO19/20 move to the CAN transceiver. Toggling DTR/RTS on
        it resets the chip, so it is opened with the lines left as the
        kernel set them.

Ports are found from the USB descriptors: sysfs on Linux, ioreg on macOS.
"""

from __future__ import annotations

import errno
import fcntl
import glob
import os
import re
import select
import subprocess
import sys
import termios
import time
from dataclasses import dataclass

CH343_VENDOR = 0x1A86
JTAG_VENDOR = 0x303A
SAMSUNG_VENDOR = 0x04E8


@dataclass
class Port:
    path: str
    vendor: int
    product: int
    serial: str
    kind: str  # "uart", "usb" or "other"

    def describe(self) -> str:
        what = {
            "uart": "terminal UART socket (CH343)",
            "usb": "terminal USB socket (ESP32-S3 USB Serial/JTAG)",
        }.get(self.kind, "other serial device")
        return f"{self.path}  {self.vendor:04x}:{self.product:04x}  {what}  {self.serial}"


def _kind(vendor: int) -> str:
    if vendor == CH343_VENDOR:
        return "uart"
    if vendor == JTAG_VENDOR:
        return "usb"
    return "other"


def _linux_ports() -> list[Port]:
    found: list[Port] = []
    for tty in sorted(glob.glob("/sys/class/tty/ttyACM*") + glob.glob("/sys/class/tty/ttyUSB*")):
        dev = os.path.realpath(os.path.join(tty, "device"))
        # Walk up from the interface to the USB device that has idVendor.
        node = dev
        for _ in range(4):
            if os.path.exists(os.path.join(node, "idVendor")):
                break
            node = os.path.dirname(node)
        try:
            with open(os.path.join(node, "idVendor")) as f:
                vendor = int(f.read().strip(), 16)
            with open(os.path.join(node, "idProduct")) as f:
                product = int(f.read().strip(), 16)
        except OSError:
            continue
        serial = ""
        try:
            with open(os.path.join(node, "serial")) as f:
                serial = f.read().strip()
        except OSError:
            pass
        path = "/dev/" + os.path.basename(tty)
        found.append(Port(path, vendor, product, serial, _kind(vendor)))
    return found


def _mac_usb_devices() -> list[dict[str, str]]:
    try:
        text = subprocess.check_output(["ioreg", "-p", "IOUSB", "-l", "-w", "0"], text=True, errors="replace")
    except (OSError, subprocess.CalledProcessError):
        return []
    devices: list[dict[str, str]] = []
    cur: dict[str, str] = {}
    for line in text.splitlines():
        if "<class IOUSBHostDevice" in line:
            if cur:
                devices.append(cur)
            cur = {}
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.split("|")[-1].strip().strip('"')
        value = value.strip().rstrip(",").strip().strip('"')
        cur[key] = value
    if cur:
        devices.append(cur)
    return devices


def parse_mac_callouts(text: str) -> dict[str, tuple[int, int, str]]:
    """Each /dev/cu.* in an IOService tree, with the USB IDs above it.

    The ESP32-S3 USB Serial/JTAG port is named after its location
    (cu.usbmodem1133201), not its serial number (the MAC, with colons), so
    matching by serial alone misses it. In ioreg's tree the serial client
    follows its USB device and interface, so the IDs seen last belong to it.
    """
    out: dict[str, tuple[int, int, str]] = {}
    vendor = product = 0
    serial = ""
    for line in text.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.split("|")[-1].strip().strip('"')
        value = value.strip().rstrip(",").strip().strip('"')
        try:
            if key == "idVendor":
                vendor = int(value)
            elif key == "idProduct":
                product = int(value)
        except ValueError:
            continue
        if key == "USB Serial Number":
            serial = value
        elif key == "IOCalloutDevice" and value.startswith("/dev/cu."):
            out[value] = (vendor, product, serial)
    return out


def _mac_callouts() -> dict[str, tuple[int, int, str]]:
    try:
        text = subprocess.check_output(["ioreg", "-p", "IOService", "-l", "-w", "0", "-r", "-c", "IOUSBHostDevice"],
                                       text=True, errors="replace")
    except (OSError, subprocess.CalledProcessError):
        return {}
    return parse_mac_callouts(text)


def _mac_ports() -> list[Port]:
    found: list[Port] = []
    paths = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.wchusbserial*"))
    claimed: set[str] = set()
    callouts = _mac_callouts()
    for p in paths:
        ids = callouts.get(p)
        if ids is not None and ids[0] in (CH343_VENDOR, JTAG_VENDOR):
            claimed.add(p)
            found.append(Port(p, ids[0], ids[1], ids[2], _kind(ids[0])))
    for dev in _mac_usb_devices():
        try:
            vendor = int(dev.get("idVendor", "0"))
            product = int(dev.get("idProduct", "0"))
        except ValueError:
            continue
        serial = dev.get("USB Serial Number") or dev.get("kUSBSerialNumberString") or ""
        if vendor == SAMSUNG_VENDOR or not serial:
            continue
        # macOS names a CDC port after the serial number (colons dropped).
        key = re.sub(r"[^0-9A-Za-z]", "", serial)
        for p in paths:
            if p in claimed:
                continue
            if key and key in re.sub(r"[^0-9A-Za-z]", "", p):
                claimed.add(p)
                found.append(Port(p, vendor, product, serial, _kind(vendor)))
    for p in paths:
        if p not in claimed:
            found.append(Port(p, 0, 0, "", "other"))
    return found


def list_ports() -> list[Port]:
    return _mac_ports() if sys.platform == "darwin" else _linux_ports()


def find_port(prefer: str = "uart", path: str | None = None) -> Port:
    """The terminal port to use. prefer: "uart", "usb" or "any"."""
    if path:
        for p in list_ports():
            if p.path == path:
                return p
        return Port(path, 0, 0, "", "other")
    ports = [p for p in list_ports() if p.kind in ("uart", "usb")]
    # The UART socket first: it never resets the chip and works on NMEA 2000.
    order = ["usb"] if prefer == "usb" else ["uart", "usb"]
    for kind in order:
        for p in ports:
            if p.kind == kind:
                return p
    raise LookupError(no_port_help(prefer))


def no_port_help(prefer: str) -> str:
    lines = ["No terminal serial port found."]
    if prefer == "usb":
        lines.append("The USB socket is only there on the simulator: on NMEA 2000 it is off by design.")
    lines.append("Plug a data cable into the socket marked UART (CH343), which works in every mode.")
    if is_wsl():
        lines.append("On WSL: `python3 tools/board.py attach` (Windows must see the device first).")
    return "\n".join(lines)


def is_wsl() -> bool:
    try:
        with open("/proc/version") as f:
            return "microsoft" in f.read().lower()
    except OSError:
        return False


class Console:
    """A raw 115200 8N1 port that never touches DTR/RTS."""

    def __init__(self, path: str):
        self.path = path
        try:
            self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        except OSError as exc:
            if exc.errno == errno.EBUSY:
                raise ConnectionError(f"{path} is already in use by another program (a console, "
                                      "monitor or second session). Close it first.") from exc
            raise
        # Exclusive: two readers on one port each get half the bytes, and
        # look like a port that keeps vanishing.
        try:
            fcntl.ioctl(self.fd, termios.TIOCEXCL)
        except OSError:
            pass
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = 0
        attrs[1] = 0
        cflag = attrs[2]
        # HUPCL off: closing must not drop DTR either.
        cflag &= ~(termios.PARENB | termios.CSTOPB | termios.CSIZE | termios.HUPCL)
        if hasattr(termios, "CRTSCTS"):
            cflag &= ~termios.CRTSCTS
        cflag |= termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[2] = cflag
        attrs[3] = 0
        attrs[4] = termios.B115200
        attrs[5] = termios.B115200
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        self._buf = b""

    def close(self) -> None:
        if self.fd >= 0:
            os.close(self.fd)
            self.fd = -1

    def __enter__(self) -> "Console":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def send(self, line: str) -> None:
        os.write(self.fd, (line.rstrip("\r\n") + "\r\n").encode("ascii", errors="replace"))

    def read(self, timeout: float) -> bytes:
        ready, _, _ = select.select([self.fd], [], [], max(timeout, 0.0))
        if not ready:
            return b""
        try:
            block = os.read(self.fd, 8192)
        except BlockingIOError:
            return b""
        if not block:
            # A CDC port that vanished (chip reset, cable out) reads empty.
            raise ConnectionError(f"{self.path} went away")
        return block

    def lines(self, seconds: float | None):
        """Yield complete log lines for `seconds`, or forever when None."""
        deadline = None if seconds is None else time.monotonic() + seconds
        while deadline is None or time.monotonic() < deadline:
            left = 0.25 if deadline is None else min(0.25, deadline - time.monotonic())
            self._buf += self.read(left)
            while b"\n" in self._buf:
                raw, self._buf = self._buf.split(b"\n", 1)
                yield raw.rstrip(b"\r").decode("utf-8", errors="replace")

    def command(self, line: str, seconds: float = 2.0) -> list[str]:
        """Send one console command and collect what comes back."""
        for _ in self.lines(0.1):
            pass
        self.send(line)
        return list(self.lines(seconds))

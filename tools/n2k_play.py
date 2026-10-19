#!/usr/bin/env python3
"""Send one NMEA console command to the Waveshare UART Type-C socket.

The CH343 (vendor 0x1A86, "USB Single Serial") does not reset the ESP32.
The socket marked USB is native USB-JTAG. Opening that port drops the chip
into the ROM download bootloader, so this script never opens it, and it
never opens the phone modem.

    python3 tools/n2k_play.py
    python3 tools/n2k_play.py PGN 130816 src=4 dead

With no arguments the command is NMEA DEMO. Extra arguments are joined
after "NMEA ".
"""

from __future__ import annotations

import glob
import os
import select
import subprocess
import sys
import termios
import time

CH343_VENDOR = 6790  # 0x1A86
USB_JTAG_VENDOR = 12346  # 0x303A
PHONE_SERIAL = "R3GL10M0WSN"


def usb_devices() -> list[dict[str, str]]:
    try:
        # -r without a name matches nothing and prints an empty tree.
        text = subprocess.check_output(
            ["ioreg", "-p", "IOUSB", "-l", "-w", "0"],
            text=True,
            errors="replace",
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        sys.exit(f"ioreg failed: {exc}")

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
        # ioreg prefixes each property with the tree glyph "|".
        key = key.split("|")[-1].strip().strip('"')
        value = value.strip().rstrip(",").strip().strip('"')
        if key in (
            "idVendor",
            "idProduct",
            "USB Product Name",
            "kUSBProductString",
            "USB Serial Number",
            "kUSBSerialNumberString",
        ):
            cur[key] = value
    if cur:
        devices.append(cur)
    return devices


def _serial_of(dev: dict[str, str]) -> str:
    return dev.get("USB Serial Number") or dev.get("kUSBSerialNumberString") or ""


def _product_of(dev: dict[str, str]) -> str:
    return dev.get("USB Product Name") or dev.get("kUSBProductString") or ""


def find_port() -> str:
    chosen = ""
    for dev in usb_devices():
        vendor = dev.get("idVendor", "")
        if vendor == str(USB_JTAG_VENDOR):
            continue
        serial = _serial_of(dev)
        if PHONE_SERIAL in serial:
            continue
        product = _product_of(dev)
        if vendor != str(CH343_VENDOR) and "USB Single Serial" not in product:
            continue
        if not serial:
            continue
        matches = sorted(glob.glob(f"/dev/cu.usbmodem{serial}*"))
        matches = [p for p in matches if PHONE_SERIAL not in p]
        if matches:
            chosen = matches[0]
            break
    if not chosen:
        sys.exit(
            "No CH343 UART port. Plug the cable into the socket marked UART "
            "(USB Single Serial), not the socket marked USB and not the phone."
        )
    return chosen


def open_raw(path: str) -> int:
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    cflag = attrs[2]
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
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def read_for(fd: int, seconds: float) -> str:
    deadline = time.monotonic() + seconds
    chunks: list[bytes] = []
    while time.monotonic() < deadline:
        left = deadline - time.monotonic()
        ready, _, _ = select.select([fd], [], [], left)
        if not ready:
            break
        try:
            block = os.read(fd, 4096)
        except BlockingIOError:
            continue
        if not block:
            break
        chunks.append(block)
    return b"".join(chunks).decode("utf-8", errors="replace")


def main() -> None:
    tail = " ".join(sys.argv[1:]).strip()
    command = f"NMEA {tail}" if tail else "NMEA DEMO"
    path = find_port()
    print(f"{path}: {command}")
    fd = open_raw(path)
    try:
        read_for(fd, 0.2)
        os.write(fd, (command + "\n").encode("ascii"))
        reply = read_for(fd, 1.0)
    finally:
        os.close(fd)
    if reply.strip():
        print(reply, end="" if reply.endswith("\n") else "\n")
    if "NMEA " in reply:
        return
    sys.exit(
        "No NMEA ack. The cable needs to be in the UART socket, and the "
        "flashed image needs NMEA DEMO. Opening the USB socket resets the chip."
    )


if __name__ == "__main__":
    main()

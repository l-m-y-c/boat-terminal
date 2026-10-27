#!/usr/bin/env python3
"""The terminal board: find it, see what it runs, get it back, flash it.

    python3 tools/board.py ports      # serial ports, which socket is which
    python3 tools/board.py status     # app / bootloader / silent / absent
    python3 tools/board.py attach     # WSL: attach via usbipd --auto-attach
    python3 tools/board.py boot       # bootloader -> app (USB socket)
    python3 tools/board.py flash      # build, upload, check it booted, recover

Works on macOS and Linux / WSL. Never toggles DTR/RTS on open.
"""

from __future__ import annotations

import argparse
import sys
import time

from lmyc_dev import board
from lmyc_dev.ports import is_wsl, list_ports


def cmd_ports(_: argparse.Namespace) -> int:
    ports = list_ports()
    if not ports:
        print("No serial ports.")
    for p in ports:
        print(p.describe())
    if is_wsl():
        hint = board.wsl_hint()
        if hint:
            print(hint)
    return 0


def cmd_status(args: argparse.Namespace) -> int:
    st = board.probe("any", args.port)
    port = f" on {st.port.path} ({st.port.kind})" if st.port else ""
    print(f"{st.state}{port}: {st.detail}")
    if st.state == "bootloader":
        print("Run `python3 tools/board.py boot` to start the app.")
    elif st.state == "absent" and is_wsl():
        print(board.wsl_hint())
    return 0 if st.state == "app" else 1


def cmd_attach(_: argparse.Namespace) -> int:
    if not is_wsl():
        print("Not WSL: nothing to attach.")
        return 0
    devs = board.terminal_usbipd()
    if not devs:
        print(board.wsl_hint())
        return 1
    for d in devs:
        print(board.attach(d, board.default_log_dir()))
    port = board.wait_for_port("any", 15)
    print(f"Port: {port.describe()}" if port else "No port appeared within 15 s.")
    return 0 if port else 1


def cmd_boot(args: argparse.Namespace) -> int:
    try:
        port = board.find_port("usb", args.port)
    except LookupError as exc:
        print(exc)
        return 1
    ok, text = board.boot_app(port)
    print(text.strip().splitlines()[-1] if text.strip() else "")
    if not ok:
        return 1
    time.sleep(3)
    st = board.probe("any")
    print(f"After reset: {st.state}: {st.detail}")
    return 0 if st.state == "app" else 1


def cmd_flash(args: argparse.Namespace) -> int:
    try:
        port = board.find_port("usb", args.port)
    except LookupError:
        print("Flashing needs the USB socket, which only exists on the simulator.\n"
              "Switch the terminal to Simulator on the Data source screen first (unplug the "
              "NMEA 2000 drop if it is on a backbone).\n" + board.uart_flash_note())
        return 1
    print(f"Uploading through {port.path}")
    if board.upload(port) != 0:
        return 1
    print("Waiting for the board to come back…")
    time.sleep(2)
    again = board.wait_for_port("usb", 20)
    if again is None:
        print("The USB port did not come back." + ("\n" + board.wsl_hint() if is_wsl() else ""))
        return 1
    st = board.probe("usb", again.path)
    if st.state == "bootloader":
        print("Left in the ROM bootloader after upload (the RTC-WDT reset). Booting the app.")
        ok, _ = board.boot_app(again)
        time.sleep(3)
        st = board.probe("any")
    print(f"{st.state}: {st.detail}")
    return 0 if st.state == "app" else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial device instead of finding it")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("ports").set_defaults(fn=cmd_ports)
    sub.add_parser("status").set_defaults(fn=cmd_status)
    sub.add_parser("attach").set_defaults(fn=cmd_attach)
    sub.add_parser("boot").set_defaults(fn=cmd_boot)
    sub.add_parser("flash").set_defaults(fn=cmd_flash)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())

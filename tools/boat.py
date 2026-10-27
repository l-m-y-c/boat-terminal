#!/usr/bin/env python3
"""The boat a terminal belongs to: its ID, name and maintenance-book post.

    python3 tools/boat.py list                 # the club fleet
    python3 tools/boat.py show                 # what this machine will build
    python3 tools/boat.py set baca-lele        # a fleet boat, by name
    python3 tools/boat.py set --id BENCH-01 --name "Bench" --sailboat 0

Writes firmware/src/secrets.h (gitignored), creating it from
secrets.h.example when it is missing, and changes only these settings:

  LMYC_BOAT_ID      the machine name: hotspot LMYC-<ID>, Bluetooth
                    LMYC-<ID>-XXXX, the pairing link. Capitals, digits and
                    '-', at most 16 characters. Changing it means phones
                    must pair again.
  LMYC_BOAT_NAME    the name crew say, on the terminal and in the app.
  LMYC_SAILBOAT_ID  the boat's WordPress post id for its maintenance book.

Then `make flash`.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path

from lmyc_dev import FIRMWARE

SECRETS = FIRMWARE / "src" / "secrets.h"
EXAMPLE = FIRMWARE / "src" / "secrets.h.example"


@dataclass(frozen=True)
class Boat:
    name: str
    sailboat_id: int

    @property
    def boat_id(self) -> str:
        return re.sub(r"[^A-Z0-9]+", "-", self.name.upper()).strip("-")

    @property
    def key(self) -> str:
        return self.boat_id.lower()


# The club fleet, from the list in secrets.h.example.
FLEET = [
    Boat("Baca Lele", 276),
    Boat("Benesse", 308),
    Boat("Chili Pepper", 505),
    Boat("Deserata", 2919),
    Boat("Scherzando", 504),
    Boat("Teachers Pet", 506),
]

_ID = re.compile(r"^[A-Z0-9][A-Z0-9-]{0,15}$")


def check_id(boat_id: str) -> str | None:
    if not _ID.match(boat_id):
        return (f"ID {boat_id!r}: use capitals, digits and '-', at most 16 characters "
                "(it becomes the Wi-Fi and Bluetooth name).")
    return None


def check_name(name: str) -> str | None:
    if not name.strip():
        return "The name is empty."
    if '"' in name or "\\" in name:
        return "The name cannot contain quotes or backslashes."
    if len(name.encode("utf-8")) > 40:
        return "The name is over 40 bytes; the terminal's title would be cut."
    return None


def find_boat(text: str) -> Boat | None:
    key = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    for b in FLEET:
        if key in (b.key, b.name.lower().replace(" ", "-")):
            return b
    return None


def read_settings(text: str) -> dict[str, str]:
    """The current #define values (strings unquoted)."""
    out: dict[str, str] = {}
    for m in re.finditer(r"^\s*#define\s+(LMYC_BOAT_ID|LMYC_BOAT_NAME|LMYC_SAILBOAT_ID)\s+(.+?)\s*$", text, re.M):
        value = m.group(2)
        if value.startswith('"') and value.endswith('"'):
            value = value[1:-1]
        out[m.group(1)] = value
    return out


def set_define(text: str, key: str, value: str) -> str:
    """Replace `#define KEY ...`, or add it after the header comment."""
    line = f"#define {key} {value}"
    pattern = re.compile(rf"^\s*#define\s+{key}\b.*$", re.M)
    if pattern.search(text):
        return pattern.sub(line, text, count=1)
    marker = "#pragma once\n"
    if marker in text:
        head, tail = text.split(marker, 1)
        return f"{head}{marker}\n{line}\n{tail}"
    return f"{line}\n{text}"


def apply(text: str, boat_id: str, name: str, sailboat_id: int) -> str:
    text = set_define(text, "LMYC_BOAT_ID", f'"{boat_id}"')
    text = set_define(text, "LMYC_BOAT_NAME", f'"{name}"')
    return set_define(text, "LMYC_SAILBOAT_ID", str(sailboat_id))


def describe(settings: dict[str, str]) -> str:
    boat_id = settings.get("LMYC_BOAT_ID", "BENCH-01 (default)")
    name = settings.get("LMYC_BOAT_NAME", "same as the ID (default)")
    sailboat = settings.get("LMYC_SAILBOAT_ID", "0 (default, unassigned)")
    plain_id = boat_id.split(" ")[0]
    return (f"  ID        {boat_id}\n"
            f"  Name      {name}\n"
            f"  Sailboat  {sailboat}\n"
            f"  Wi-Fi     LMYC-{plain_id}\n"
            f"  Bluetooth LMYC-{plain_id}-XXXX (last two bytes of the MAC)")


def cmd_list(_: argparse.Namespace) -> int:
    for b in FLEET:
        print(f"  {b.key:14s} {b.name:14s} ID {b.boat_id:14s} sailboat {b.sailboat_id}")
    return 0


def cmd_show(_: argparse.Namespace) -> int:
    if not SECRETS.exists():
        print(f"{SECRETS} does not exist yet: builds use the defaults (BENCH-01).")
        return 1
    print(f"{SECRETS}:")
    print(describe(read_settings(SECRETS.read_text(encoding="utf-8"))))
    return 0


def cmd_set(args: argparse.Namespace) -> int:
    boat = find_boat(args.boat) if args.boat else None
    if args.boat and boat is None:
        print(f"No fleet boat called {args.boat!r}. `python3 tools/boat.py list`, or give --id and --name.")
        return 1
    boat_id = args.id or (boat.boat_id if boat else None)
    name = args.name or (boat.name if boat else None)
    sailboat = args.sailboat if args.sailboat is not None else (boat.sailboat_id if boat else None)
    if boat_id is None or name is None or sailboat is None:
        print("Give a fleet boat, or all of --id, --name and --sailboat.")
        return 1
    for problem in (check_id(boat_id), check_name(name)):
        if problem:
            print(problem)
            return 1
    if not SECRETS.exists():
        shutil.copyfile(EXAMPLE, SECRETS)
        print(f"Created {SECRETS} from the example. Fill in Wi-Fi there if the boat has an uplink.")
    text = SECRETS.read_text(encoding="utf-8")
    before = read_settings(text)
    after_text = apply(text, boat_id, name, sailboat)
    if after_text == text:
        print("Already set:")
        print(describe(read_settings(text)))
        return 0
    SECRETS.write_text(after_text, encoding="utf-8")
    print("Set:")
    print(describe(read_settings(after_text)))
    old_id = before.get("LMYC_BOAT_ID", "BENCH-01")
    if old_id != boat_id:
        print(f"\nThe ID changed from {old_id}: the hotspot and Bluetooth names change too. "
              "Phones paired with the old name must pair again (scan the new QR).")
    print("\nNext: make flash")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list").set_defaults(fn=cmd_list)
    sub.add_parser("show").set_defaults(fn=cmd_show)
    p = sub.add_parser("set")
    p.add_argument("boat", nargs="?", help="a fleet boat, e.g. baca-lele")
    p.add_argument("--id")
    p.add_argument("--name")
    p.add_argument("--sailboat", type=int)
    p.set_defaults(fn=cmd_set)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())

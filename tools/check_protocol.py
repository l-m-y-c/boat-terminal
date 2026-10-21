#!/usr/bin/env python3
"""Fail when the terminal <-> phone contract drifts between languages.

The firmware, the Flutter app and the maintenance stand-in each declare the
BLE UUIDs, ports and maintenance-book limits by hand. This reads all three
without building or running anything and compares them:

    python3 tools/check_protocol.py

Exit status is 1 on any mismatch or missing name.
"""

from __future__ import annotations

import ast
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

C_FILES = [
    ROOT / "firmware/src/protocol.h",
    ROOT / "firmware/src/snapshot.h",
]
DART_FILES = [
    ROOT / "mobile/lib/protocol.dart",
    ROOT / "mobile/lib/boatlink.dart",
]
STAND_IN = ROOT / "tools/maint_card_server.py"
STATS_C = ROOT / "firmware/src/terminal_stats.cpp"
STATS_DART = ROOT / "mobile/lib/terminal_stats.dart"

# Names checked between firmware and app. Every one must exist on both sides.
SHARED = [
    "kLmycServiceUuid",
    "kLmycPayloadUuid",
    "kLmycSessionUuid",
    "kLmycStatusUuid",
    "kLmycNmeaUuid",
    "kLmycWifiUuid",
    "kLmycStatsUuid",
    "kLmycBlePrefix",
    "kSoftApIp",
    "kGatewayTcpPort",
    "kGatewayUdpPort",
    "kGatewayUdpAltPort",
    "kMaintTitleMax",
    "kMaintDescMax",
    "kMaintReporterMax",
    "kMaintPhotoMax",
    "kMaintPhotoBytesMax",
    "kMaintPhotoNoteMax",
    "kMaintBodyMax",
    "kMaintCategories",
    "kMaintPriorities",
    "kMaintStatuses",
    "kSnapshotJpegMax",
]

# Firmware name -> stand-in name. Lists compare as sets (the stand-in uses sets).
STAND_IN_NAMES = {
    "kMaintTitleMax": "TITLE_MAX",
    "kMaintDescMax": "DESC_MAX",
    "kMaintReporterMax": "REPORTER_MAX",
    "kMaintPhotoMax": "PHOTO_COUNT_MAX",
    "kMaintPhotoBytesMax": "PHOTO_MAX",
    "kMaintPhotoNoteMax": "NOTE_MAX",
    "kMaintBodyMax": "BODY_MAX",
    "kMaintCategories": "CATEGORIES",
    "kMaintPriorities": "PRIORITIES",
    "kMaintStatuses": "STATUSES",
}


def _arith(expr: str) -> int:
    """Evaluate a constant like `500 * 1024` without eval()."""
    tree = ast.parse(expr.strip(), mode="eval")

    def walk(node: ast.AST) -> int:
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return node.value
        if isinstance(node, ast.BinOp):
            left, right = walk(node.left), walk(node.right)
            if isinstance(node.op, ast.Mult):
                return left * right
            if isinstance(node.op, ast.Add):
                return left + right
            if isinstance(node.op, ast.Sub):
                return left - right
        raise ValueError(f"not a constant: {expr!r}")

    return walk(tree.body)


def _strings(body: str) -> list[str]:
    return re.findall(r"""["']([^"']*)["']""", body)


def read_c(paths: list[Path]) -> dict[str, object]:
    out: dict[str, object] = {}
    for path in paths:
        text = re.sub(r"/\*.*?\*/", "", path.read_text(), flags=re.S)
        for name, value in re.findall(
            r"static\s+constexpr\s+const\s+char\s*\*\s*(\w+)\s*=\s*\"([^\"]*)\"\s*;", text
        ):
            out[name] = value
        for name, body in re.findall(
            r"static\s+constexpr\s+const\s+char\s*\*\s*(\w+)\s*\[\]\s*=\s*\{(.*?)\}\s*;",
            text,
            flags=re.S,
        ):
            out[name] = _strings(body)
        for name, expr in re.findall(
            r"static\s+constexpr\s+(?:size_t|uint16_t|uint32_t|int)\s+(\w+)\s*=\s*([^;]+);",
            text,
        ):
            try:
                out[name] = _arith(expr)
            except (SyntaxError, ValueError):
                pass
    return out


def read_dart(paths: list[Path]) -> dict[str, object]:
    out: dict[str, object] = {}
    for path in paths:
        text = re.sub(r"//[^\n]*", "", path.read_text())
        for name, body in re.findall(
            r"const\s+(?:List<String>\s+)?(\w+)\s*=\s*(?:const\s+)?<String>\[(.*?)\]\s*;",
            text,
            flags=re.S,
        ):
            out[name] = _strings(body)
        for name, value in re.findall(r"const\s+(?:String\s+)?(\w+)\s*=\s*'([^']*)'\s*;", text):
            out[name] = value
        for name, expr in re.findall(r"const\s+(?:int\s+)?(\w+)\s*=\s*([0-9][0-9 *+\-]*)\s*;", text):
            out[name] = _arith(expr)
    return out


def read_stand_in(path: Path) -> dict[str, object]:
    out: dict[str, object] = {}
    for node in ast.parse(path.read_text()).body:
        if not (isinstance(node, (ast.Assign, ast.AnnAssign))):
            continue
        targets = node.targets if isinstance(node, ast.Assign) else [node.target]
        for target in targets:
            if isinstance(target, ast.Name) and target.id in STAND_IN_NAMES.values():
                try:
                    out[target.id] = ast.literal_eval(node.value)
                except ValueError:
                    out[target.id] = _arith(ast.unparse(node.value))
    return out


def stats_fields() -> tuple[set[str], set[str], set[str]]:
    """Keys the firmware prints, keys the app reads, and the state codes of each."""
    c = STATS_C.read_text()
    dart = STATS_DART.read_text()
    printed = set(re.findall(r'\\"([a-z_]+)\\":', c))
    read = set(re.findall(r"n\('([a-z_]+)'\)", dart)) | set(re.findall(r"decoded\['([a-z_]+)'\]", dart))
    c_states = set(re.findall(r'return "([a-z_]+)";', c.split("state_code", 1)[1].split("}\n", 1)[0]))
    dart_states = set(re.findall(r"'([a-z_]+)': BusState\.", dart))
    return printed, read, c_states ^ dart_states


def main() -> int:
    firmware = read_c(C_FILES)
    app = read_dart(DART_FILES)
    stand_in = read_stand_in(STAND_IN)
    problems: list[str] = []

    for name in SHARED:
        fw, ph = firmware.get(name), app.get(name)
        if fw is None:
            problems.append(f"{name}: missing from firmware ({', '.join(p.name for p in C_FILES)})")
        if ph is None:
            problems.append(f"{name}: missing from app ({', '.join(p.name for p in DART_FILES)})")
        if fw is None or ph is None:
            continue
        same = set(fw) == set(ph) if isinstance(fw, list) else fw == ph
        if not same:
            problems.append(f"{name}: firmware {fw!r} != app {ph!r}")

    for name, py_name in STAND_IN_NAMES.items():
        fw, py = firmware.get(name), stand_in.get(py_name)
        if py is None:
            problems.append(f"{py_name}: missing from {STAND_IN.name}")
            continue
        same = set(fw or []) == set(py) if isinstance(py, (set, list)) else fw == py
        if not same:
            problems.append(f"{py_name}: stand-in {py!r} != firmware {name} {fw!r}")

    printed, read, state_drift = stats_fields()
    for key in sorted(read - printed):
        problems.append(f"stats JSON: the app reads {key!r}, which {STATS_C.name} does not print")
    for code in sorted(state_drift - {"unknown"}):
        problems.append(f"stats JSON: state {code!r} is known to only one of firmware and app")

    if problems:
        print("Protocol drift between firmware, app and stand-in:", file=sys.stderr)
        for line in problems:
            print(f"  - {line}", file=sys.stderr)
        return 1
    print(
        f"protocol: {len(SHARED)} shared names, {len(STAND_IN_NAMES)} stand-in limits "
        f"and {len(read)} stats fields agree"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

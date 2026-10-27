"""Shared code for the boat-terminal developer tools (tools/*.py).

Standard library only, so every tool runs with any Python 3.11+ on the Mac
or on Linux / WSL without installing anything.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
FIRMWARE = ROOT / "firmware"

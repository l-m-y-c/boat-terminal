# LMYC Boat Terminal — developer workflow
#
# Prefer these targets over ad-hoc flutter/pio commands so both sides stay in sync.
#
#   make help
#   make flash              Build + upload terminal firmware, check it booted
#   make session            Boat mode: console log + diagnostics snapshots (tools/README.md)
#   make phone              Run app (quick iteration)
#   make phone-reinstall    Clean rebuild + install (use after BLE/native changes)
#   make pair-test          Flash terminal, then clean-reinstall phone app
#   make test               Everything CI runs that needs no board or phone
#   make devices            List serial + Flutter devices
#   make clean              Clean both projects

.PHONY: boat-diag phone-bundle help all test firmware firmware-test n2k-demo flash monitor phone phone-get phone-run \
        phone-reinstall phone-test phone-analyze protocol-check pair-test devices doctor \
        clean clean-firmware clean-phone flash-pio board-ports board-status board-attach board-boot \
        console send diag watch session bus-check bus-status record nmea-tcp capture-summary replay \
        sim-capture tools-test network device network-wifi boat boat-show boat-list

FIRMWARE_DIR := firmware
MOBILE_DIR   := mobile

# Python for tools/: uv's own interpreter when uv is installed, never the OS one.
PY := $(shell command -v uv >/dev/null 2>&1 && echo "uv run -q --no-project python" || echo python3)

help:
	@echo ""
	@echo "LMYC Boat Terminal"
	@echo "=================="
	@echo ""
	@echo "Firmware (ESP32 terminal)"
	@echo "  make firmware          Build only"
	@echo "  make firmware-test     Host tests: JSON, N2K -> 0183, AIS (no board)"
	@echo "  make n2k-demo          Send canned PGNs on the UART Type-C socket"
	@echo "  make flash             Build + upload (press RESET after if needed)"
	@echo "  make monitor           Serial monitor @ 115200"
	@echo ""
	@echo "Phone (Flutter / Android)"
	@echo "  make phone             Quick run (hot-restart friendly)"
	@echo "  make phone-reinstall   flutter clean + pub get + run"
	@echo "                         Use this after BLE / native / permission changes"
	@echo "  make phone-get         flutter pub get only"
	@echo "  make phone-test        flutter test"
	@echo "  make phone-analyze     dart format check + flutter analyze"
	@echo ""
	@echo "Combined"
	@echo "  make all               Build firmware + resolve mobile deps"
	@echo "  make test              firmware-test + phone-analyze + phone-test + protocol-check"
	@echo "  make protocol-check    Firmware, app and stand-in declare the same UUIDs and limits"
	@echo "  make pair-test         Flash terminal + phone-reinstall"
	@echo "  make devices           PlatformIO serial + Flutter devices"
	@echo "  make doctor            flutter doctor"
	@echo "  make clean             Clean firmware and mobile"
	@echo ""
	@echo "Typical bench loop after pulling:"
	@echo "  git pull && make pair-test"
	@echo "  → terminal DIAGNOSTICS should say Advertising"
	@echo "  → phone: Find terminals (not Android Settings)"
	@echo "  → scan NEW QR on terminal → Connect & pair"
	@echo "  (OOB / QR secret is regenerated every boot)"
	@echo ""

# ---------------------------------------------------------------------------
# Firmware
# ---------------------------------------------------------------------------

firmware:
	cd $(FIRMWARE_DIR) && pio run

# Runs on the Mac or Linux. Needs the NMEA2000 library that `make firmware`
# fetches. CXX picks the compiler (default c++).
firmware-test:
	sh $(FIRMWARE_DIR)/test_host/run.sh

# CH343 UART socket only. Does not open the USB socket (that resets the chip).
n2k-demo:
	$(PY) tools/n2k_play.py

# Upload through the USB socket, then check the app booted; recovers the board
# from the ROM bootloader the upload reset can leave it in.
flash:
	$(PY) tools/board.py flash
	@echo ""
	@echo "OOB / QR secret is regenerated every boot — always rescan QR after flash."
	@echo ""

# Plain PlatformIO upload, no checks.
flash-pio:
	cd $(FIRMWARE_DIR) && pio run -t upload

monitor:
	cd $(FIRMWARE_DIR) && pio device monitor -b 115200

# ---------------------------------------------------------------------------
# Mobile
# ---------------------------------------------------------------------------

phone-get:
	cd $(MOBILE_DIR) && flutter pub get

# Fast path for UI-only iteration (may not pick up all native/BLE changes)
phone phone-run: phone-get
	cd $(MOBILE_DIR) && flutter run

# Play Store upload bundle (docs/23-play-store.md). Signed with the upload key
# from $$LMYC_KEY_PROPERTIES or ~/.android-keys/lmyc-boat/key.properties;
# without it Gradle warns and signs with the debug key (Play rejects that).
phone-bundle: phone-get
	cd $(MOBILE_DIR) && flutter build appbundle --release
	@echo "→ $(MOBILE_DIR)/build/app/outputs/bundle/release/app-release.aab"

phone-test: phone-get
	cd $(MOBILE_DIR) && flutter test

phone-analyze: phone-get
	cd $(MOBILE_DIR) && dart format --output=none --set-exit-if-changed lib test
	cd $(MOBILE_DIR) && flutter analyze

# Full reinstall — use whenever BLE, permissions, or native code changed
phone-reinstall:
	@echo "Clean rebuild of mobile app (avoids stale BLE / requestMtu binaries)…"
	cd $(MOBILE_DIR) && flutter clean
	cd $(MOBILE_DIR) && flutter pub get
	cd $(MOBILE_DIR) && flutter run

# ---------------------------------------------------------------------------
# Combined / utilities
# ---------------------------------------------------------------------------

all: firmware phone-get
	@echo "Firmware built; mobile dependencies resolved."

protocol-check:
	$(PY) tools/check_protocol.py

# The checks CI runs that need no board or phone. firmware-test needs one
# `make firmware` first so the NMEA2000 library is on disk.
test: firmware-test phone-analyze phone-test protocol-check tools-test
	@echo "All host checks passed."

# Full bench path: new firmware (new OOB) + clean phone binary
pair-test: flash phone-reinstall

devices:
	@echo "=== PlatformIO serial devices ==="
	-cd $(FIRMWARE_DIR) && pio device list
	@echo ""
	@echo "=== Flutter devices ==="
	-flutter devices

doctor:
	flutter doctor

clean-firmware:
	cd $(FIRMWARE_DIR) && pio run -t clean

clean-phone:
	cd $(MOBILE_DIR) && flutter clean

clean: clean-firmware clean-phone
	@echo "Cleaned firmware and mobile."

# ---------------------------------------------------------------------------
# Developer tools (tools/README.md). The serial ones prefer the UART socket,
# which works on NMEA 2000 and never resets the chip.
# ---------------------------------------------------------------------------

board-ports:
	$(PY) tools/board.py ports

board-status:
	$(PY) tools/board.py status

# WSL: attach the board with usbipd --auto-attach (survives every reset).
board-attach:
	$(PY) tools/board.py attach

# ROM bootloader -> app, through the USB socket.
board-boot:
	$(PY) tools/board.py boot

console:
	$(PY) tools/console.py log --tee console-$$(date +%Y%m%d).log

# make send CMD="NMEA STATS"
send:
	$(PY) tools/console.py send $(CMD)

diag:
	$(PY) tools/console.py diag

watch:
	$(PY) tools/console.py watch --every 10

# Boat mode: full log + a diagnostics snapshot every 10 s, saved in sessions/.
session:
	$(PY) tools/console.py session --every 10

# Over the boat Wi-Fi (the terminal's hotspot). HOST=127.0.0.1:10180 through the phone relay.
HOST ?= 192.168.4.1
bus-check:
	$(PY) tools/terminal.py --host $(HOST) bus

bus-status:
	$(PY) tools/terminal.py --host $(HOST) status

# make record SECONDS=120 OUT=dock.log
SECONDS ?= 60
OUT ?= capture-$$(date +%Y%m%d-%H%M%S).log
record:
	$(PY) tools/terminal.py --host $(HOST) record --seconds $(SECONDS) --out $(OUT)

# Bench pull after a boat session: UART, Wi-Fi rings and the phone's relay
# diary into sessions/<now>/ (tools/boat_diag.py). HOST as above, or
# VIA_PHONE=1 to reach the terminal through the phone's relay over USB.
boat-diag:
	$(PY) tools/boat_diag.py --host $(HOST) $(if $(VIA_PHONE),--via-phone)

nmea-tcp:
	$(PY) tools/terminal.py --nmea-host $(HOST) nmea --seconds 30

# make capture-summary CAP=dock.log [TERMINAL=23]
capture-summary:
	$(PY) tools/capture.py summary $(CAP) $(if $(TERMINAL),--terminal $(TERMINAL))

# A recording through the real library, diagnostics and converter on the host.
# make replay CAP=dock.log
replay:
	$(PY) tools/capture.py replay $(CAP) --sentences $(CAP).0183.txt --json $(CAP).n2k.json

sim-capture:
	$(PY) tools/capture.py sim 60 sim-capture.log

tools-test:
	$(PY) -m unittest discover -s tools/tests

# The NMEA 2000 network: over the UART console, or the boat Wi-Fi.
network:
	$(PY) tools/console.py net

# make device SRC=35
device:
	$(PY) tools/console.py net --src $(SRC)

network-wifi:
	$(PY) tools/terminal.py --host $(HOST) network

# The boat this terminal belongs to (firmware/src/secrets.h). Then make flash.
# make boat BOAT=baca-lele
boat:
	$(PY) tools/boat.py set $(BOAT)

boat-show:
	$(PY) tools/boat.py show

boat-list:
	$(PY) tools/boat.py list

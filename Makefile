# LMYC Boat Terminal — developer workflow
#
# Prefer these targets over ad-hoc flutter/pio commands so both sides stay in sync.
#
#   make help
#   make flash              Build + upload terminal firmware
#   make phone              Run app (quick iteration)
#   make phone-reinstall    Clean rebuild + install (use after BLE/native changes)
#   make pair-test          Flash terminal, then clean-reinstall phone app
#   make test               Everything CI runs that needs no board or phone
#   make devices            List serial + Flutter devices
#   make clean              Clean both projects

.PHONY: help all test firmware firmware-test n2k-demo flash monitor phone phone-get phone-run \
        phone-reinstall phone-test phone-analyze protocol-check pair-test devices doctor \
        clean clean-firmware clean-phone

FIRMWARE_DIR := firmware
MOBILE_DIR   := mobile

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
	python3 tools/n2k_play.py

flash:
	cd $(FIRMWARE_DIR) && pio run -t upload
	@echo ""
	@echo "Upload done. Press RESET on the board if the UI does not start."
	@echo "OOB / QR secret is regenerated every boot — always rescan QR after flash."
	@echo ""

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
	python3 tools/check_protocol.py

# The checks CI runs that need no board or phone. firmware-test needs one
# `make firmware` first so the NMEA2000 library is on disk.
test: firmware-test phone-analyze phone-test protocol-check
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

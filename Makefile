# LMYC Boat Terminal — developer workflow
#
# Common targets:
#   make help          Show this help
#   make all           Build firmware + resolve mobile deps
#   make flash         Build + upload firmware to the ESP32 terminal
#   make phone         Build + install + run the Flutter app on a connected phone
#   make pair-test     Flash terminal, then run the phone app (full bench loop)
#   make clean         Clean both projects
#
# Requirements:
#   - PlatformIO Core (`pio`) on PATH
#   - Flutter SDK on PATH
#   - Terminal on USB (for flash)
#   - Android phone with USB debugging (for phone)

.PHONY: help all firmware firmware-upload flash monitor phone phone-get phone-run \
        pair-test clean clean-firmware clean-phone devices doctor

FIRMWARE_DIR := firmware
MOBILE_DIR   := mobile

help:
	@echo ""
	@echo "LMYC Boat Terminal"
	@echo "=================="
	@echo ""
	@echo "Firmware (ESP32 terminal)"
	@echo "  make firmware         Build only"
	@echo "  make flash            Build + upload (hold BOOT if needed)"
	@echo "  make monitor          Serial monitor at 115200"
	@echo "  make firmware-upload  Alias for flash"
	@echo ""
	@echo "Phone (Flutter / Android)"
	@echo "  make phone-get        flutter pub get"
	@echo "  make phone            Build, install, and run on connected device"
	@echo "  make phone-run        Same as phone"
	@echo ""
	@echo "Combined"
	@echo "  make all              Build firmware + mobile deps"
	@echo "  make pair-test        Flash terminal, then run phone app"
	@echo "  make devices          List PlatformIO serial + Flutter devices"
	@echo "  make doctor           flutter doctor"
	@echo "  make clean            Clean both projects"
	@echo ""

# ---------------------------------------------------------------------------
# Firmware
# ---------------------------------------------------------------------------

firmware:
	cd $(FIRMWARE_DIR) && pio run

firmware-upload flash:
	cd $(FIRMWARE_DIR) && pio run -t upload
	@echo ""
	@echo "Upload done. Press RESET on the board if the UI does not start."
	@echo "OOB / QR secret is regenerated every boot — rescan QR after flash."
	@echo ""

monitor:
	cd $(FIRMWARE_DIR) && pio device monitor -b 115200

# ---------------------------------------------------------------------------
# Mobile
# ---------------------------------------------------------------------------

phone-get:
	cd $(MOBILE_DIR) && flutter pub get

phone phone-run: phone-get
	cd $(MOBILE_DIR) && flutter run

# ---------------------------------------------------------------------------
# Combined / utilities
# ---------------------------------------------------------------------------

all: firmware phone-get
	@echo "Firmware built; mobile dependencies resolved."

pair-test: flash
	@echo ""
	@echo "Terminal flashed. Starting phone app…"
	@echo "Scan the (new) QR on the terminal, then tap Connect & pair."
	@echo ""
	@$(MAKE) phone

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

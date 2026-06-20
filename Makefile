.PHONY: all clean test test-cpp help ios ios-signed xcode native deploy deploy-app deploy-ios run run-app run-ios \
	        install-deps ios-icons app-icons scrub update-dbc \
	        firmware firmware-flash flash flash-usb monitor firmware-port capture capture-usb startup-log firmware-clean \
	        capture-ota capture-tcp ota-keys flash-ota flash-tcp reboot-over-usb reboot-over-tcp

# Device ID (first connected/available device, excluding unavailable)
DEVICE_ID ?= $(shell xcrun devicectl list devices 2>/dev/null | awk 'NR>1 && !/unavailable/ && match($$0, /[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}/) { print substr($$0, RSTART, RLENGTH); exit }')

# ESP32 firmware config
FIRMWARE_DIR  = firmware/can-bridge
FIRMWARE_BUILD = build-firmware
FQBN          = esp32:esp32:esp32:PartitionScheme=min_spiffs
ESP32_PORT    ?= $(shell ls /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART /dev/cu.wchusbserial* 2>/dev/null | head -1)
ESPTOOL       ?= $(firstword $(wildcard $(HOME)/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool))
ESP32_HOST    ?= $(OTA_HOST)

         RED=\033[0;31m
       GREEN=\033[0;32m
        BLUE=\033[0;34m
      PURPLE=\033[0;35m
        CYAN=\033[0;36m
      YELLOW=\033[1;33m
       WHITE=\033[1;37m
	      NC=\033[0m

# Default -- build + test all platforms
all: test firmware ios

# -- Clean ---------------------------------------------------------------

clean: clean-icons
	rm -rf build-native build-ios $(FIRMWARE_BUILD)
	rm -rf ~/Library/Developer/Xcode/DerivedData/VehicleSimApp-*
	rm -rf vehicle-sim-ios/VehicleSim/build

scrub: clean
	@echo "Scrubbing all caches..."
	rm -rf ~/Library/Developer/Xcode/DerivedData/*
	rm -rf ~/Library/Developer/Xcode/Archives/*
	rm -rf ~/Library/Developer/Xcode/iOS\ DeviceSupport/*
	rm -f vehicle-sim-ios/VehicleSim/Assets.xcassets/AppIcon.appiconset/*.png
	rm -f .firmware-ready
	@echo "All cleaned. Run 'make' to rebuild."

# -- Native C++ ----------------------------------------------------------

build-native/Makefile: CMakeLists.txt
	@mkdir -p build-native
	@cd build-native && cmake .. -DBUILD_IOS=OFF

native: build-native/Makefile
	@$(MAKE) -C build-native all

# C++ gtest suite (green) + Python capture-notepad suite (must also be green).
# A failure in either fails the build.
TEST_REPORT = build-native/test-report.txt

test: native test-cpp

test-cpp: $(TEST_REPORT)
	@cat "$(TEST_REPORT)"

$(TEST_REPORT): build-native/Makefile CMakeLists.txt test/CMakeLists.txt $(shell find test include src -type f 2>/dev/null)
	@echo "--- Running C++ tests ---"
	@$(MAKE) -C build-native vehicle-sim-tests
	@$(MAKE) -C build-native test ARGS="--verbose" GTEST_COLOR=yes | tee "$(TEST_REPORT)"

# -- iOS ------------------------------------------------------------------

verify-device-id:
	@if [ -z "$(DEVICE_ID)" ]; then \
		echo "\033[31mError: no connected iPhone found. Connect and trust your device.\033[0m" >&2; \
		exit 1; \
	fi; \
	echo -e "\033[32mFound device: $(DEVICE_ID)\033[0m";
	@xcrun devicectl list devices

ios: test native app-icons
	@echo "--- Building iOS app for Simulator (Debug) ---"
	@xcodebuild -project vehicle-sim-ios/VehicleSim/VehicleSimApp.xcodeproj -scheme VehicleSimApp -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 16' -derivedDataPath vehicle-sim-ios/VehicleSim/build build 2>&1 | tail -10

ios-signed: test native app-icons
	@echo "--- Building Release for Physical iOS Device ---"
	@xcodebuild -project vehicle-sim-ios/VehicleSim/VehicleSimApp.xcodeproj -scheme VehicleSimApp -configuration Release -destination 'generic/platform=iOS' -derivedDataPath vehicle-sim-ios/VehicleSim/build -allowProvisioningUpdates clean build 2>&1 | tail -20
	@echo "Build output in vehicle-sim-ios/VehicleSim/build/Release-iphoneos/VehicleSimApp.app"

deploy deploy-app deploy-ios: ios-signed verify-device-id
	@echo "--- Installing on connected iPhone ---"
	APP_PATH="$(PWD)/vehicle-sim-ios/VehicleSim/build/Build/Products/Release-iphoneos/VehicleSimApp.app"; \
	xcrun devicectl device install app --device "$(DEVICE_ID)" "$$APP_PATH"
	@echo "--- Deploy complete ---"

run run-app run-ios: deploy verify-device-id
	@echo "--- Launching app on iPhone ---"
	@xcrun devicectl device process launch --terminate-existing --device "$(DEVICE_ID)" com.axxiant.vehiclesim
	@echo "--- App launched ---"

xcode: native app-icons
	@echo "Launching Xcode..."
	@open vehicle-sim-ios/VehicleSim/VehicleSimApp.xcodeproj

# -- iOS Icons -------------------------------------------------------------

ICON_LIGHT = image/ODB2_car_logo_trans.png
ICON_DARK  = image/ODB2_car_logo_white_trans.png

ICON_CATALOG = vehicle-sim-ios/VehicleSim/Assets.xcassets/AppIcon.appiconset
ICON_FILES = \
	$(ICON_CATALOG)/AppIcon.png \
	$(ICON_CATALOG)/AppIcon-dark.png

ios-icons: $(ICON_FILES)
app-icons: $(ICON_FILES)

clean-icons:
	@rm -f $(ICON_CATALOG)/*.png

define generate_icon
@mkdir -p "$(ICON_CATALOG)"
@IMGT=$$(command -v magick 2>/dev/null); \
	if [ -z "$$IMGT" ]; then echo "Error: ImageMagick not installed. Run 'make install-deps'." >&2; exit 1; fi; \
	echo "  Generating $@ (1024x1024) from $<"; \
	$$IMGT "$<" -trim +repage -resize "1600x1600" -background transparent -gravity center -extent "1024x1024" "$@"
endef

$(ICON_CATALOG)/AppIcon.png: $(ICON_LIGHT)
	$(generate_icon)

$(ICON_CATALOG)/AppIcon-dark.png: $(ICON_DARK)
	$(generate_icon)

# -- DBC ------------------------------------------------------------------
#
# ANALYSIS: update-dbc vs git submodule
#
# Q: What does update-dbc do that "git submodule update --remote" doesn't?
#
# A: Two things:
#
# 1. Renaming. The submodule file is called tesla_model3_party.dbc but the
#    project references it as Model3CAN.dbc (see DefaultVehicleConfigs.cpp).
#    update-dbc copies AND renames it in one step. A raw git submodule update
#    would leave the file at external/opendbc/opendbc/dbc/tesla_model3_party.dbc
#    and the build would not find it.
#
# 2. Selective copy. The opendbc submodule contains hundreds of DBC files for
#    many manufacturers. update-dbc copies only the two files the project
#    actually uses (tesla_model3_party.dbc and vw_mlb.dbc) into the project's
#    own resources/dbc/ directory, keeping the repo self-contained.
#
# Could we use the submodule directly? In theory yes -- we could change
# DefaultVehicleConfigs.cpp to point at external/opendbc/opendbc/dbc/ and use
# the upstream filenames. But that would couple the build to the submodule's
# internal directory structure, which commaai can change without notice. The
# copy step is a deliberate boundary: it lets us pin filenames, insulate from
# upstream renames, and keep the build working even without the submodule
# checked out (e.g. in CI or for new clones that forget git submodule init).
#
# Verdict: the copy is justified. Keep update-dbc.

update-dbc:
	@echo "Updating DBC files from commaai/opendbc submodule..."
	@cd external/opendbc && git checkout master && git pull
	@cp external/opendbc/opendbc/dbc/tesla_model3_party.dbc resources/dbc/Model3CAN.dbc
	@cp external/opendbc/opendbc/dbc/vw_mlb.dbc resources/dbc/vw_mlb.dbc
	@echo "DBC files updated. Model3CAN.dbc is the canonical tesla_model3_party.dbc (verbatim)."

# -- Dependencies ----------------------------------------------------------

install-deps:
	@echo "Installing build dependencies from Brewfile..."
	@brew bundle --no-upgrade
	@echo "All dependencies installed."

# -- ESP32 Firmware (opt-in) ----------------------------------------------
#
# make firmware            -- build firmware
# make flash               -- test + build + flash to ESP32
# make monitor             -- serial console (live view)
# make capture             -- log CAN frames to captures/<tag>_<ts>.raw.txt + .csv
#
# WiFi: set ESP32_WIFI_SSID and ESP32_WIFI_PASSWORD env vars (e.g. in .zshrc)
#       Credentials are injected as compiler defines -- never written to disk
#       Falls back to AP mode (ESP32-CAN / cancan12) if not set
#

ESP32_WIFI_SSID ?=
ESP32_WIFI_PASSWORD ?=

# WiFi credentials as compiler defines (only in memory, never on disk)
ifneq ($(ESP32_WIFI_SSID),)
FIRMWARE_CFLAGS = -DESP32_WIFI_SSID=$(ESP32_WIFI_SSID) -DESP32_WIFI_PASSWORD=$(ESP32_WIFI_PASSWORD)
endif

# Force a rebuild whenever WiFi env vars change. Without this, arduino-cli can
# reuse an old binary and silently keep the previous SSID/password baked in.
FIRMWARE_WIFI_SENTINEL = $(FIRMWARE_BUILD)/.wifi-env

$(FIRMWARE_WIFI_SENTINEL):
	@mkdir -p $(FIRMWARE_BUILD)
	@printf '%s\n%s\n' "$(ESP32_WIFI_SSID)" "$(ESP32_WIFI_PASSWORD)" > "$@"

$(FIRMWARE_BUILD)/can-bridge.ino.bin: $(wildcard $(FIRMWARE_DIR)/*.ino) $(FIRMWARE_WIFI_SENTINEL)
	@mkdir -p $(FIRMWARE_BUILD)
	@echo "--- Building ESP32 firmware ---"
	@arduino-cli compile --fqbn $(FQBN) $(FIRMWARE_DIR) --output-dir $(FIRMWARE_BUILD) \
		--build-property "compiler.cpp.extra_flags=$(FIRMWARE_CFLAGS)"
	@$(ESPTOOL) \
		--chip esp32 \
		merge-bin --output $(FIRMWARE_BUILD)/can-bridge.ino.merged.bin \
		--target-offset 0x0 \
		0x1000 $(FIRMWARE_BUILD)/can-bridge.ino.bootloader.bin \
		0x8000 $(FIRMWARE_BUILD)/can-bridge.ino.partitions.bin \
		0x10000 $(FIRMWARE_BUILD)/can-bridge.ino.bin

firmware: $(FIRMWARE_BUILD)/can-bridge.ino.bin

flash flash-usb firmware-flash: firmware native test
	@if [ -z "$(ESP32_PORT)" ]; then echo "Error: no ESP32 serial port detected. Plug in the board. Override with: make flash ESP32_PORT=/dev/cu.XXXX" >&2; exit 1; fi
	@echo "Flashing via $(ESP32_PORT)..."
	@$(ESPTOOL) \
		--port "$(ESP32_PORT)" --baud 460800 \
		write_flash 0x0 $(FIRMWARE_BUILD)/can-bridge.ino.merged.bin
	@echo "Flash complete. Reading startup log from $(ESP32_PORT) at 115200 baud..."
	@$(MAKE) startup-log ESP32_PORT="$(ESP32_PORT)"
	@echo "Startup log complete. Run 'make capture CAPFILE=<name>' to log CAN frames."

startup-log:
	@if [ -z "$(ESP32_PORT)" ]; then echo "Error: no ESP32 serial port detected. Plug in the board. Override with: make startup-log ESP32_PORT=/dev/cu.XXXX" >&2; exit 1; fi
	@scripts/serial-startup-log.pl --port "$(ESP32_PORT)" --baud 115200 --max-wait 30 --post-byte 30

firmware-port:
	@if [ -z "$(ESP32_PORT)" ]; then echo "No ESP32 detected. Plug in via USB and check with: ls /dev/cu.usb* /dev/cu.SLAB* /dev/cu.wchusbserial*" exit 1; fi
	@echo "$(ESP32_PORT)"

monitor:
	@if [ -z "$(ESP32_PORT)" ]; then echo "Error: no ESP32 serial port detected." >&2; exit 1; fi
	@screen "$(ESP32_PORT)" 115200

reboot-over-usb:
	@if [ -z "$(ESP32_PORT)" ]; then echo "Error: no ESP32 serial port detected. Plug in the board. Override with: make reboot-over-usb ESP32_PORT=/dev/cu.XXXX" >&2; exit 1; fi
	@if [ -z "$(ESPTOOL)" ]; then echo "Error: esptool not found under $(HOME)/Library/Arduino15/packages/esp32/tools/esptool_py/." >&2; exit 1; fi
	@echo "Starting serial logger, then resetting $(ESP32_PORT) via esptool USB control reset..."
	@scripts/serial-startup-log.pl --port "$(ESP32_PORT)" --baud 115200 --max-wait 30 --post-byte 30 --reset-esptool --esptool "$(ESPTOOL)"

# -- ESP32 OTA (signed-image, over-WiFi) -----------------------------------
#
# make ota-keys    -- generate your per-user ed25519 signing keypair + bake
#                    the public key into firmware/can-bridge/OtaPublicKey.h
# make flash-ota   -- sign the built firmware + push it to an ESP32 over WiFi
#
# ESP32_HOST the ESP32 IP/hostname to push to (REQUIRED for flash-ota / capture-ota)
# OTA_PORT   the OTA listener port (3334, distinct from the 3333 CAN bridge)
# OTA_KEYS_DIR  per-user signing keypair dir (NEVER committed)
#
# NOTE: the FIRST time you change your keypair, the new public key must be
#       flashed over USB (make flash) so the device trusts it. Subsequent
#       updates can go over WiFi via flash-ota.

OTA_HOST     ?=
OTA_PORT     ?= 3334
OTA_KEYS_DIR ?= $(HOME)/.vehicle-sim/ota

ota-keys:
	@echo "--- Generating per-user OTA signing keypair ---"
	@scripts/ota-generate-keys.sh --keys-dir "$(OTA_KEYS_DIR)"
	@echo ""
	@echo "IMPORTANT: the public key was baked into firmware/can-bridge/OtaPublicKey.h."
	@echo "The FIRST time you use this keypair you MUST re-flash over USB so the"
	@echo "device trusts it:  make flash"
	@echo "Subsequent updates can be pushed over WiFi:  make flash-ota ESP32_HOST=<ip>"

discover: native
	@./build-native/vehicle-sim --discover

flash-ota flash-tcp: firmware native
	@if [ -z "$(ESP32_HOST)" ]; then \
		echo "--- Auto-discovering ESP32 ---" && \
		DISCOVERED_IP=$$(./build-native/vehicle-sim --discover 2>/dev/null | \
			grep -oE 'tcp:[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+' | head -1 | cut -d: -f2); \
		if [ -z "$$DISCOVERED_IP" ]; then \
			echo "Error: could not auto-discover ESP32. Set ESP32_HOST=<ip> or use --discover." >&2; \
			exit 1; \
		fi; \
		echo "--- Found ESP32 at $$DISCOVERED_IP ---"; \
		ESP32_HOST="$$DISCOVERED_IP"; \
	fi
	@echo "--- Signing firmware ---"
	@scripts/ota-sign.sh $(FIRMWARE_BUILD)/can-bridge.ino.bin --keys-dir "$(OTA_KEYS_DIR)"
	@echo "--- Pushing OTA image to $(ESP32_HOST):$(OTA_PORT) ---"
	@scripts/ota-flash.sh "$(ESP32_HOST)" $(FIRMWARE_BUILD)/can-bridge.ino.bin \
		--sig $(FIRMWARE_BUILD)/can-bridge.ino.bin.sig --port $(OTA_PORT)

reboot-over-tcp:
	@if [ -z "$(ESP32_HOST)" ]; then echo "Error: ESP32_HOST is required. Usage: make reboot-over-tcp ESP32_HOST=<esp32-ip>" >&2; exit 1; fi
	@echo "Soft rebooting $(ESP32_HOST) via TCP command..."
	@printf 'ATZ\rATE0\rATREBOOT\r' | nc -w 2 "$(ESP32_HOST)" 3333 || true

# -- Capture (native USB) --------------------------------------------------
#
# Capture the ESP32 serial stream to TWO timestamped files (RAW + CSV) using
# the native vehicle-sim binary (USBTransport). No Python dependency.
#
#   make capture                       -> captures/capture_<ts>.raw.txt + .csv
#   CAPFILE=TeslaMonday make capture    -> captures/TeslaMonday_<ts>.raw.txt + .csv
#
# RAW  = verbatim safety net: every transport line written to <base>.raw.txt.
# CSV  = parsed/filtered: one row per VALID frame ``timestamp_ms,can_id,dlc,
#        data_hex``; status/corrupt/noise lines go to RAW ONLY.
# Both files share the same ``<tag>_<ts>`` stem with .raw.txt/.csv extensions
# so the pair is obvious. STDOUT echoes decoded telemetry live. Ctrl-C to stop.
#
# Uses the native vehicle-sim binary (USBTransport via --connect usb:<port>).
# The Python POC (scripts/archive/serial_to_csv.py) is archived and no longer
# used by this target.
CAPFILE ?=
CAPDIR  ?= captures
CAPTURE_VEHICLE ?= tesla

capture capture-usb: native
	@if [ -z "$(ESP32_PORT)" ]; then echo "Error: no ESP32 serial port detected." >&2; exit 1; fi
	@mkdir -p $(CAPDIR)
	@ts=$$(date +%Y-%m-%d-%H%M%S); \
		if [ -n "$(CAPFILE)" ]; then name="$(CAPFILE)_$$ts"; else name="capture_$$ts"; fi; \
		base="$(CAPDIR)/$$name"; \
		echo "$(GREEN)Capturing$(NC) $(ESP32_PORT) -> $(CYAN)$$base.raw.txt $(NC)(verbatim) + $(CYAN)$$base.csv $(NC)(decoded) $(GREEN)(Ctrl-C to stop)$(NC)"; \
		PS4=''; set -x;\
		./build-native/vehicle-sim --connect "usb:$(ESP32_PORT)" --vehicle $(CAPTURE_VEHICLE) --log "$$base";


# -- Capture (WiFi / TCP) --------------------------------------------------
#
# Same as capture-usb but connects over TCP to ESP32_HOST instead of USB serial.
#
#   make capture-ota                       -> captures/capture_<ts>.raw.txt + .csv
#   CAPFILE=TeslaMonday make capture-ota    -> captures/TeslaMonday_<ts>.raw.txt + .csv
#
# Requires ESP32_HOST to be set (the ESP32 IP/hostname on the local network).

capture-ota capture-tcp: native
	@if [ -z "$(ESP32_HOST)" ]; then echo "Error: ESP32_HOST is required. Usage: make capture-ota ESP32_HOST=<esp32-ip>" >&2; exit 1; fi
	@mkdir -p $(CAPDIR)
	@ts=$$(date +%Y-%m-%d-%H%M%S); \
		if [ -n "$(CAPFILE)" ]; then name="$(CAPFILE)_$$ts"; else name="capture_$$ts"; fi; \
		base="$(CAPDIR)/$$name"; \
		echo "$(GREEN)Capturing$(NC) $(ESP32_HOST) -> $(CYAN)$$base.raw.txt $(NC)(verbatim) + $(CYAN)$$base.csv $(NC)(decoded) $(GREEN)(Ctrl-C to stop)$(NC)"; \
		PS4=''; set -x;\
		./build-native/vehicle-sim --connect "tcp:$(ESP32_HOST)" --vehicle $(CAPTURE_VEHICLE) --log "$$base";


# -- Help ------------------------------------------------------------------

help:
	@echo "Available targets:"
	@echo "  all              - Build all platforms: native, tests, firmware, iOS"
	@echo "  native           - Build native macOS C++ libraries"
	@echo "  test             - Run C++ unit tests"
	@echo "  firmware         - Build ESP32 CAN bridge firmware"
	@echo "  flash            - Build + flash firmware to ESP32 via USB (aliases: flash-usb, firmware-flash), then print startup log"
	@echo "  discover         - Discover ESP32 devices on the local network via UDP broadcast"
	@echo "  flash-ota        - Sign + push firmware to ESP32 over WiFi (alias: flash-tcp; requires ESP32_HOST=<ip>)"
	@echo "  monitor          - Serial monitor at 115200 baud (live view)"
	@echo "  startup-log      - Wait up to 30s for startup bytes, then read for 30s"
	@echo "  reboot-over-usb  - Send ATREBOOT over USB serial, then print startup log"
	@echo "  reboot-over-tcp  - Send ATREBOOT over TCP port 3333 (requires ESP32_HOST=<esp32-ip>)"
	@echo "  capture          - Log CAN frames via USB (aliases: capture-usb)"
	@echo "  capture-ota      - Log CAN frames over WiFi TCP (alias: capture-tcp; requires ESP32_HOST=<esp32-ip>)"
	@echo "  firmware-port    - Show detected ESP32 serial port"
	@echo "  ota-keys         - Generate per-user ed25519 OTA signing keypair + bake public key"
	@echo "  ios              - Build iOS app for simulator (Debug)"
	@echo "  ios-signed       - Build signed Release for physical device"
	@echo "  deploy           - Deploy to connected iPhone (aliases: deploy-app, deploy-ios)"
	@echo "  run              - Deploy and launch on device (aliases: run-app, run-ios)"
	@echo "  xcode            - Open in Xcode"
	@echo "  install-deps     - Install Homebrew dependencies"
	@echo "  update-dbc       - Update DBC files from opendbc"
	@echo "  clean            - Clean build artifacts"
	@echo "  scrub            - Full clean including toolchain sentinel"
	@echo "  help             - Show this help message"

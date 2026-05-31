.PHONY: all clean test help ios ios-signed xcode native deploy run \
        install-deps ios-icons app-icons scrub update-dbc \
        firmware firmware-flash flash firmware-monitor firmware-port

# Device ID (first connected/available device, excluding unavailable)
DEVICE_ID ?= $(shell xcrun devicectl list devices 2>/dev/null | awk 'NR>1 && !/unavailable/ && match($$0, /[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}/) { print substr($$0, RSTART, RLENGTH); exit }')

# ESP32 firmware config
FIRMWARE_DIR  = firmware/can-bridge
FIRMWARE_BUILD = build-firmware
FQBN          = esp32:esp32:esp32
ESP32_PORT    ?= $(shell ls /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART /dev/cu.wchusbserial* 2>/dev/null | head -1)

# Default — build + test all platforms
all: test firmware ios

# ── Clean ──────────────────────────────────────────────────────────────

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

# ── Native C++ ─────────────────────────────────────────────────────────

build-native/Makefile: CMakeLists.txt
	@mkdir -p build-native
	@cd build-native && cmake .. -DBUILD_IOS=OFF

native: build-native/Makefile
	@$(MAKE) -C build-native all

test: native
	@$(MAKE) -C build-native vehicle-sim-tests
	@$(MAKE) -C build-native test ARGS="--verbose" GTEST_COLOR=yes

# ── iOS ─────────────────────────────────────────────────────────────────

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

deploy: ios-signed verify-device-id
	@echo "--- Installing on connected iPhone ---"
	APP_PATH="$(PWD)/vehicle-sim-ios/VehicleSim/build/Build/Products/Release-iphoneos/VehicleSimApp.app"; \
	xcrun devicectl device install app --device "$(DEVICE_ID)" "$$APP_PATH"
	@echo "--- Deploy complete ---"

run: deploy verify-device-id
	@echo "--- Launching app on iPhone ---"
	@xcrun devicectl device process launch --terminate-existing --device "$(DEVICE_ID)" com.axxiant.vehiclesim
	@echo "--- App launched ---"

xcode: native app-icons
	@echo "Launching Xcode..."
	@open vehicle-sim-ios/VehicleSim/VehicleSimApp.xcodeproj

# ── iOS Icons ───────────────────────────────────────────────────────────

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

# ── DBC ─────────────────────────────────────────────────────────────────

update-dbc:
	@echo "Updating DBC files from commaai/opendbc submodule..."
	@cd external/opendbc && git checkout master && git pull
	@cp external/opendbc/opendbc/dbc/tesla_can.dbc resources/dbc/Model3CAN.dbc
	@cp external/opendbc/opendbc/dbc/vw_mlb.dbc resources/dbc/vw_mlb.dbc
	@echo "DBC files updated."

# ── Dependencies ────────────────────────────────────────────────────────

install-deps:
	@echo "Installing build dependencies from Brewfile..."
	@brew bundle --no-upgrade
	@echo "All dependencies installed."

# ── ESP32 Firmware (opt-in) ────────────────────────────────────────────
#
# make firmware            — build firmware
# make flash               — test + build + flash to ESP32
# make firmware-monitor    — serial console
#
# WiFi: set ESP32_WIFI_SSID and ESP32_WIFI_PASSWORD env vars (e.g. in .zshrc)
#       Credentials are injected as compiler defines — never written to disk
#       Falls back to AP mode (ESP32-CAN / cancan12) if not set
#

ESP32_WIFI_SSID ?=
ESP32_WIFI_PASSWORD ?=

# WiFi credentials as compiler defines (only in memory, never on disk)
ifneq ($(ESP32_WIFI_SSID),)
FIRMWARE_CFLAGS = -DESP32_WIFI_SSID=$(ESP32_WIFI_SSID) -DESP32_WIFI_PASSWORD=$(ESP32_WIFI_PASSWORD)
endif

# One-time setup sentinel — only runs when .firmware-ready doesn't exist
.firmware-ready:
	@command -v arduino-cli >/dev/null 2>&1 || { echo "Installing arduino-cli..."; brew install arduino-cli; }
	@echo "Setting up ESP32 toolchain (one-time)..."
	@arduino-cli config init --overwrite 2>/dev/null || true
	@arduino-cli config set board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
	@arduino-cli core update-index 2>/dev/null
	@arduino-cli core install esp32:esp32
	@touch .firmware-ready

# Only recompile when source files change or setup runs
$(FIRMWARE_BUILD)/can-bridge.ino.bin: $(wildcard $(FIRMWARE_DIR)/*.ino) .firmware-ready
	@mkdir -p $(FIRMWARE_BUILD)
	@echo "--- Building ESP32 firmware ---"
	@arduino-cli compile --fqbn $(FQBN) $(FIRMWARE_DIR) --output-dir $(FIRMWARE_BUILD) \
		--build-property "compiler.cpp.extra_flags=$(FIRMWARE_CFLAGS)"

firmware: $(FIRMWARE_BUILD)/can-bridge.ino.bin

flash firmware-flash: test firmware
	@if [ -z "$(ESP32_PORT)" ]; then echo "Error: no ESP32 serial port detected. Plug in the board. Override with: make flash ESP32_PORT=/dev/cu.XXXX" >&2; exit 1; fi
	@echo "Flashing via $(ESP32_PORT)..."
	@$(HOME)/Library/Arduino15/packages/esp32/tools/esptool_py/5.2.0/esptool \
		--port "$(ESP32_PORT)" --baud 460800 \
		write-flash 0x0 $(FIRMWARE_BUILD)/can-bridge.ino.merged.bin
	@echo "Flash complete."
	@echo ""
	@echo "  Serial monitor starting — press Ctrl-A then k then y to quit"
	@echo ""
	@sleep 2
	@screen "$(ESP32_PORT)" 115200

firmware-port:
	@if [ -z "$(ESP32_PORT)" ]; then echo "No ESP32 detected. Plug in via USB and check with: ls /dev/cu.usb* /dev/cu.SLAB* /dev/cu.wchusbserial*"; exit 1; fi
	@echo "$(ESP32_PORT)"

firmware-monitor:
	@if [ -z "$(ESP32_PORT)" ]; then echo "Error: no ESP32 serial port detected." >&2; exit 1; fi
	@screen "$(ESP32_PORT)" 115200

# ── Help ────────────────────────────────────────────────────────────────

help:
	@echo "Available targets:"
	@echo "  all              - Build all platforms: native, tests, firmware, iOS"
	@echo "  native           - Build native macOS C++ libraries"
	@echo "  test             - Run C++ unit tests"
	@echo "  firmware         - Build ESP32 CAN bridge firmware"
	@echo "  flash            - Build + flash firmware to ESP32 (alias: firmware-flash)"
	@echo "  firmware-monitor - Serial monitor at 115200 baud"
	@echo "  firmware-port    - Show detected ESP32 serial port"
	@echo "  ios              - Build iOS app for simulator (Debug)"
	@echo "  ios-signed       - Build signed Release for physical device"
	@echo "  deploy           - Deploy to connected iPhone"
	@echo "  run              - Deploy and launch on device"
	@echo "  xcode            - Open in Xcode"
	@echo "  install-deps     - Install Homebrew dependencies"
	@echo "  update-dbc       - Update DBC files from opendbc"
	@echo "  clean            - Clean build artifacts"
	@echo "  scrub            - Full clean including toolchain sentinel"
	@echo "  help             - Show this help message"

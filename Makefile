.PHONY: all clean test help ios ios-signed xcode native deploy run install-deps ios-icons app-icons scrub update-dbc

# Device ID (first connected/available device, excluding unavailable)
DEVICE_ID ?= $(shell xcrun devicectl list devices 2>/dev/null | awk 'NR>1 && !/unavailable/ && match($$0, /[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}/) { print substr($$0, RSTART, RLENGTH); exit }')

# Default target - build native + test + iOS
all: test ios

# Clean build artifacts (keep generated icons)
clean: clean-icons
	rm -rf build-native build-ios
	rm -rf ~/Library/Developer/Xcode/DerivedData/VehicleSimApp-*
	rm -rf vehicle-sim-ios/VehicleSim/build

# Update DBC files from commaai/opendbc submodule
update-dbc:
	@echo "Updating DBC files from commaai/opendbc submodule..."
	@cd external/opendbc && git checkout master && git pull
	@cp external/opendbc/opendbc/dbc/tesla_can.dbc resources/dbc/Model3CAN.dbc
	@cp external/opendbc/opendbc/dbc/vw_mlb.dbc resources/dbc/vw_mlb.dbc
	@echo "DBC files updated."

# Full scrub: clean + Xcode caches + generated icons (keeps Contents.json)
scrub: clean
	@echo "Scrubbing all build caches and generated icons..."
	rm -rf ~/Library/Developer/Xcode/DerivedData/*
	rm -rf ~/Library/Developer/Xcode/Archives/*
	rm -rf ~/Library/Developer/Xcode/iOS\ DeviceSupport/*
	# Only remove generated PNG files, keep Contents.json (catalog definition)
	rm -f vehicle-sim-ios/VehicleSim/Assets.xcassets/AppIcon.appiconset/*.png
	@echo "All cleaned. Run 'make' to rebuild."

# Run tests
test: native
	@$(MAKE) -C build-native vehicle-sim-tests
	@$(MAKE) -C build-native test ARGS="--verbose" GTEST_COLOR=yes

verify-device-id:
	@if [ -z "$(DEVICE_ID)" ]; then \
		echo "\033[31mError: no connected iPhone found. Connect and trust your device.\033[0m" >&2; \
		exit 1; \
	fi; \
	echo -e "\033[32mFound device: $(DEVICE_ID)\033[0m";
	@xcrun devicectl list devices

# Build iOS app for simulator (Debug)
ios: test native app-icons
	@echo "--- Building iOS app for Simulator (Debug) ---"
	@xcodebuild -project vehicle-sim-ios/VehicleSim/VehicleSimApp.xcodeproj -scheme VehicleSimApp -configuration Debug -destination 'platform=iOS Simulator,name=iPhone 16' -derivedDataPath vehicle-sim-ios/VehicleSim/build build 2>&1 | tail -10

# Build signed Release for physical device
ios-signed: test native app-icons
	@echo "--- Building Release for Physical iOS Device ---"
	@xcodebuild -project vehicle-sim-ios/VehicleSim/VehicleSimApp.xcodeproj -scheme VehicleSimApp -configuration Release -destination 'generic/platform=iOS' -derivedDataPath vehicle-sim-ios/VehicleSim/build -allowProvisioningUpdates clean build 2>&1 | tail -20
	@echo "Build output in vehicle-sim-ios/VehicleSim/build/Release-iphoneos/VehicleSimApp.app"

# Deploy signed build to attached iPhone (install only)
deploy: ios-signed verify-device-id
	@echo "--- Installing on connected iPhone ---"
	APP_PATH="$(PWD)/vehicle-sim-ios/VehicleSim/build/Build/Products/Release-iphoneos/VehicleSimApp.app"; \
	xcrun devicectl device install app --device "$(DEVICE_ID)" "$$APP_PATH"
	@echo "--- Deploy complete ---"

# Deploy and launch
run: deploy verify-device-id 
	@echo "--- Launching app on iPhone ---"
	@xcrun devicectl device process launch --terminate-existing --device "$(DEVICE_ID)" com.axxiant.vehiclesim
	@echo "--- App launched ---"

# Open Xcode (ensures project is ready)
xcode: native app-icons
	@echo "Launching Xcode..."
	@open vehicle-sim-ios/VehicleSim/VehicleSimApp.xcodeproj

# Build native libraries
native:
	@if [ ! -d build-native ]; then mkdir -p build-native; fi
	@cd build-native && cmake .. -DBUILD_IOS=OFF
	@$(MAKE) -C build-native all

# === iOS Icon Generation ===
# Source images live in image/
# Requires: brew install imagemagick
#
# iOS 18+ uses "universal" icon format: single 1024x1024 per appearance.
# No per-size icons needed — Xcode generates all sizes from the 1024 source.

# Light mode (default): dark logo on transparent
# Dark mode: glow logo on transparent
ICON_LIGHT = image/ODB2_car_logo_trans.png
ICON_DARK  = image/ODB2_car_logo_white_trans.png

ICON_CATALOG = vehicle-sim-ios/VehicleSim/Assets.xcassets/AppIcon.appiconset
ICON_FILES = \
	$(ICON_CATALOG)/AppIcon.png \
	$(ICON_CATALOG)/AppIcon-dark.png

# Generate icons (no build)
ios-icons: $(ICON_FILES)

# Generate icons and copy into Xcode project (dependency for ios/xcode/app-signed)
app-icons: $(ICON_FILES)

# Remove stale generated icons (keeps Contents.json)
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

# Install build dependencies via Homebrew
install-deps:
	@echo "Installing build dependencies from Brewfile..."
	@brew bundle --no-upgrade
	@echo "All dependencies installed."

# Help
help:
	@echo "Available targets:"
	@echo "  all        - Build native C++ libs, run tests, and build iOS app"
	@echo "  native     - Build native macOS C++ libraries (required for iOS)"
	@echo "  test       - Run C++ unit tests (depends on native)"
	@echo "  ios        - Build iOS app for simulator (Debug) — depends on native + icons"
	@echo "  ios-signed - Build signed Release IPA for physical device — depends on native + icons"
	@echo "  deploy     - Deploy Release build to connected iPhone (install only)"
	@echo "  run        - Deploy Release build and launch on device (depends on deploy)"
	@echo "  xcode      - Build native + icons, then open Xcode project"
	@echo "  app-icons  - Generate iOS app icons (light + dark variants)"
	@echo "  install-deps - Install ImageMagick, cmake via Homebrew"
	@echo "  update-dbc - Update DBC files from commaai/opendbc submodule"
	@echo "  clean      - Clean build artifacts (keeps generated icons)"
	@echo "  scrub      - Full clean: DerivedData, caches, and generated icons"
	@echo "  help       - Show this help message"

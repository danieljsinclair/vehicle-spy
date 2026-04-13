.PHONY: all clean test help ios xcode native

# Default target - build native
all: native

# Clean build artifacts
clean:
	rm -rf build-native build-ios

# Run tests (builds everything including test binary first)
test: native
	@$(MAKE) -C build-native vehicle-sim-tests
	@$(MAKE) -C build-native test ARGS="--verbose" GTEST_COLOR=yes

# Build C++ library for iOS, headless build the Xcode app
ios:
	@if [ ! -d build-ios ]; then mkdir build-ios; fi
	@cd build-ios && cmake .. -DBUILD_IOS=ON -DBUILD_TESTS=OFF
	@$(MAKE) -C build-ios all
	@echo "--- Building iOS app ---"
	@set -o pipefail && xcodebuild -project vehicle-sim-ios/VehicleSim/VehicleSimApp.xcodeproj -scheme VehicleSimApp -destination 'platform=iOS Simulator,name=iPhone 16' build 2>&1 | tail -5

# Open in Xcode (runs ios build first)
xcode: ios
	@open vehicle-sim-ios/VehicleSim/VehicleSimApp.xcodeproj

native:
	@if [ ! -d build-native ]; then mkdir build-native; fi
	@cd build-native && cmake .. -DBUILD_IOS=OFF
	@$(MAKE) -C build-native all

# Show help
help:
	@echo "Available targets:"
	@echo "  all     - Build native CLI (default)"
	@echo "  clean   - Clean build artifacts"
	@echo "  test    - Run unit tests (88 passing)"
	@echo "  ios     - Build iOS app headlessly (C++ lib + Swift app)"
	@echo "  xcode   - Build iOS app and open in Xcode (for device deploy)"
	@echo "  native  - Build native macOS CLI"
	@echo "  help    - Show this help message"

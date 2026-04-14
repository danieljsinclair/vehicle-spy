.PHONY: all clean test help ios xcode native

# Default target - build native
all: test ios

# Clean build artifacts
clean:
	rm -rf build-native build-ios
	rm -rf ~/Library/Developer/Xcode/DerivedData
	rm -rf vehicle-sim-ios/VehicleSim/build

# Run tests (builds everything including test binary first)
test: native
	@$(MAKE) -C build-native vehicle-sim-tests
	@$(MAKE) -C build-native test ARGS="--verbose" GTEST_COLOR=yes

# Build iOS app (Xcode compiles C++ sources directly — no prebuilt library needed)
ios:
	@echo "--- Building iOS app ---"
	@set -o pipefail && xcodebuild -project vehicle-sim-ios/VehicleSim/VehicleSimApp.xcodeproj -target VehicleSimApp -destination 'platform=iOS Simulator,name=iPhone 16' build 2>&1 | tail -10

# Open in Xcode (runs ios build first)
xcode: ios
	@echo "Launching xcode..."
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
	@echo "  test    - Run unit tests"
	@echo "  ios     - Build iOS app (Xcode compiles C++ directly)"
	@echo "  xcode   - Build iOS app and open in Xcode (for device deploy)"
	@echo "  native  - Build native macOS CLI"
	@echo "  help    - Show this help message"

.PHONY: all clean test help ios native

# Default target
all:
	@$(MAKE) -C build all

# Clean build artifacts
clean:
	@$(MAKE) -C build clean

# Run tests
test:
	@$(MAKE) -C build test

# Build for iOS
ios:
	@if [ ! -d build-ios ]; then mkdir build-ios; fi
	@cd build-ios && cmake .. -DBUILD_IOS=ON -DCMAKE_TOOLCHAIN_FILE=../cmake/iOS.cmake
	@$(MAKE) -C build-ios all

# Build for native platform
native:
	@if [ ! -d build-native ]; then mkdir build-native; fi
	@cd build-native && cmake .. -DBUILD_IOS=OFF
	@$(MAKE) -C build-native all

# Show help
help:
	@echo "Available targets:"
	@echo "  all     - Build all targets (default)"
	@echo "  clean   - Clean build artifacts"
	@echo "  test    - Run tests"
	@echo "  ios     - Build for iOS platform"
	@echo "  native  - Build for native platform"
	@echo "  help    - Show this help message"

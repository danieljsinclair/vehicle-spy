.PHONY: all clean test test-cpp help ios ios-signed xcode native deploy deploy-app deploy-ios run run-app run-ios \
	        install-deps ios-icons app-icons scrub update-dbc firmware-wifi-sentinel \
	        firmware firmware-flash flash flash-usb monitor firmware-port capture capture-usb startup-log firmware-clean \
	        capture-tcp ota-keys flash-over-tcp flash-over-usb reboot-over-usb reboot-over-tcp check-esp32 \
	        sonar-scan sonar-summary sonar-compiledb sonar-clean summary \
			header

# Device ID (first connected/available device, excluding unavailable)
DEVICE_ID ?= $(shell xcrun devicectl list devices 2>/dev/null | awk 'NR>1 && !/unavailable/ && match($$0, /[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}/) { print substr($$0, RSTART, RLENGTH); exit }')

# ESP32 firmware config
FIRMWARE_DIR  = firmware/can-bridge
FIRMWARE_BUILD = build-firmware
FQBN          = esp32:esp32:esp32:PartitionScheme=min_spiffs
ESP32_PORT    ?= $(shell ls /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART /dev/cu.wchusbserial* 2>/dev/null | head -1)
ESPTOOL_DIR   ?= $(shell python3 -c 'import pathlib; roots=sorted(pathlib.Path.home().glob("Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool"), key=lambda p: tuple(int(x) if x.isdigit() else x for x in p.parent.name.split("."))); print(roots[-1].parent if roots else "")')
ESPTOOL       ?= $(ESPTOOL_DIR)/esptool
ESPTOOL_MERGE_CMD ?= $(shell if [ -x "$(ESPTOOL)" ] && "$(ESPTOOL)" --help 2>/dev/null | grep -q 'merge-bin'; then printf 'merge-bin'; else printf 'merge_bin'; fi)
ESP32_HOST    ?=
ESP32_WIFI_PASS ?=

         RED=\033[0;31m
       GREEN=\033[0;32m
        BLUE=\033[0;34m
      PURPLE=\033[0;35m
        CYAN=\033[0;36m
      YELLOW=\033[1;33m
       WHITE=\033[1;37m
	      NC=\033[0m

# Default -- build + test all platforms + sonar scan (if sources changed).
# `sonar-scan` is dependency-gated: runs only when firmware sources changed
# (and shows the FULL sonar-summary after a real scan). When up-to-date, skips
# silently. `summary` (the ONE-LINE headline) is ALWAYS the last output.
all: header test firmware ios footer sonar-scan summary

# Shared macro to show build config (DRY)
define show_wifi
	@if [ -n "$(ESP32_WIFI_SSID)" ]; then \
		echo " ESP32_WIFI_SSID: $(GREEN)$(ESP32_WIFI_SSID)$(NC)"; \
		_pass="$(ESP32_WIFI_PASS)"; \
		_masked=$$(echo "$$_pass" | sed 's/./\*/g'); \
		_masked=$${_pass%"$${_pass#?}"}$${_masked#?}; \
		echo " ESP32_WIFI_PASS: $(RED)$${_masked}$(NC)"; \
	else \
		echo "       WiFi: $(YELLOW)AP mode (ESP32-CAN)$(NC)"; \
	fi
endef

define show_config
	@echo ""
	@echo "--- Build Configuration ---"
	$(show_wifi)
	@echo "      ESP32_HOST: $(ESP32_HOST)"
	@echo "      ESP32_PORT: $(PURPLE)$(ESP32_PORT)$(NC)"
endef

header:
	$(show_config)
	@echo "    FIRMWARE_DIR: $(CYAN)$(FIRMWARE_DIR)$(NC)"
	@echo "  FIRMWARE_BUILD: $(FIRMWARE_BUILD)"
	@echo ""

footer:
	$(show_config)
	@echo "  Run 'make check-esp32 ESP32_HOST=<ip>' to verify device connectivity"
	@echo ""

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
	@cd build-native && cmake .. -DBUILD_IOS=OFF -DBUILD_TESTS=ON

native macos osx: build-native/Makefile
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
		echo "${RED}Error: no connected iPhone found. Connect and trust your device.${NC}" >&2; \
		exit 1; \
	fi; \
	echo -e "${GREEN}Found device: $(DEVICE_ID)${NC}";
	@xcrun devicectl list devices

# Shared xcodebuild runner (DRY for ios / ios-signed).
# Per-target config is passed via TARGET-SPECIFIC variables (XCB_CONFIG /
# XCB_DEST / XCB_EXTRA), NOT via $(call ...) arguments — because the destination
# string contains a comma, which would split $(call) args and corrupt the
# command line.
#
# Streams xcodebuild output LIVE to the terminal (progress is visible) while
# teeing a full copy to build-ios/xcodebuild.XXXXXX for post-mortem. `set -o
# pipefail` makes the pipeline's exit status reflect xcodebuild's real result
# (tee otherwise masks it), so the single coloured verdict line below is driven
# by the actual exit code and `exit $$XCB_STATUS` propagates failure to make.
# pipefail is supported by bash and zsh (and macOS /bin/sh).
#
# mktemp: BSD mkstemp needs the X's to be the TRAILING chars of the template, so
# build-ios/xcodebuild.XXXXXX (NOT ....XXXXXX.log). A suffix after the X's leaves
# them un-substituted and fails "File exists" on the second run.
define run_xcodebuild
	@set -o pipefail; \
	echo "--- Building iOS app ($(XCB_CONFIG)) ---"; \
	mkdir -p build-ios; \
	XCODEBUILD_LOG=$$(mktemp build-ios/xcodebuild.XXXXXX); \
	xcodebuild -project vehicle-sim-ios/VehicleSim/VehicleSimApp.xcodeproj \
			   -scheme VehicleSimApp -configuration $(XCB_CONFIG) -destination "$(XCB_DEST)" \
			   -derivedDataPath vehicle-sim-ios/VehicleSim/build $(XCB_EXTRA) build 2>&1 | tee $$XCODEBUILD_LOG; \
	XCB_STATUS=$$?; \
	if [ $$XCB_STATUS -eq 0 ]; then \
		printf "${GREEN}== iOS BUILD SUCCEEDED ==${NC}  (logfile: ${CYAN}$$XCODEBUILD_LOG${NC})\n"; \
	else \
		printf "${RED}== iOS BUILD FAILED ==${NC}  (logfile: ${CYAN}$$XCODEBUILD_LOG${NC})\n"; \
	fi; \
	exit $$XCB_STATUS
endef

ios: XCB_CONFIG := Debug
ios: XCB_DEST := platform=iOS Simulator,name=iPhone 16
ios: XCB_EXTRA :=
ios: test native app-icons
	$(run_xcodebuild)

ios-signed: XCB_CONFIG := Release
ios-signed: XCB_DEST := generic/platform=iOS
ios-signed: XCB_EXTRA := -allowProvisioningUpdates clean
ios-signed: test native app-icons
	$(run_xcodebuild)

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
	if [ -z "$$IMGT" ]; then echo "${RED}Error: ImageMagick not installed. Run 'make install-deps'.${NC}" >&2; exit 1; fi; \
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
# WiFi: set ESP32_WIFI_SSID and ESP32_WIFI_PASS env vars (e.g. in .zshrc)
#       Credentials are injected as compiler defines -- never written to disk
#       Falls back to AP mode (ESP32-CAN / cancan12) if not set
#

# WiFi credentials: read from environment, or prompt at build time.
# Never use a silent default — the wrong SSID means the ESP32 won't connect.
ESP32_WIFI_SSID ?=
ESP32_WIFI_PASS ?=
FIRMWARE_EXTRA_CFLAGS ?=

# WiFi credentials as compiler defines (only in memory, never on disk)
ifneq ($(ESP32_WIFI_SSID),)
FIRMWARE_CFLAGS += -DESP32_WIFI_SSID=$(ESP32_WIFI_SSID) -DESP32_WIFI_PASS=$(ESP32_WIFI_PASS)
else
$(warning ESP32_WIFI_SSID is not set. Firmware will use AP mode (ESP32-CAN).)
$(warning Set: export ESP32_WIFI_SSID=manht2 ESP32_WIFI_PASS=yourpassword)
endif

# TCP auth token — single credential for both TCP commands and OTA
# Generated by make ota-creds, persisted in ~/.zshrc, baked into firmware at build time
ESP32_TCP_TOKEN ?= vehicle-sim-2026
FIRMWARE_CFLAGS += -DTCP_AUTH_TOKEN=\"$(ESP32_TCP_TOKEN)\"

# Force a re-bake when WiFi credentials or TCP token change.
#
# Make cannot observe command-line variable changes (e.g. ESP32_WIFI_SSID=...),
# so a stale sentinel with old creds would never refresh and the firmware would
# silently keep the previous credentials baked in. The sentinel recipe therefore
# ALWAYS runs (via FORCE) and recomputes the md5 of the credential values; it
# rewrites the file ONLY when the hash actually changed, so can-bridge.ino.bin
# rebuilds exactly when a credential changed -- not on every invocation with the
# same creds.
FIRMWARE_CRED_SENTINEL = $(FIRMWARE_BUILD)/.cred-hash
FORCE:

$(FIRMWARE_CRED_SENTINEL): $(FIRMWARE_DIR)/*.ino FORCE
	@mkdir -p $(FIRMWARE_BUILD)
	@_new=$$(printf '%s%s%s' '$(ESP32_WIFI_SSID)' '$(ESP32_WIFI_PASS)' '$(ESP32_TCP_TOKEN)' | md5sum | awk '{print $$1}'); \
	_old=$$(cat $@ 2>/dev/null || true); \
	if [ "$$_new" != "$$_old" ]; then \
		printf '%s\n' "$$_new" > $@; \
		echo "  ${CYAN}WiFi credentials changed${NC} -> rebuilding firmware..."; \
	fi

$(FIRMWARE_BUILD)/can-bridge.ino.bin: $(FIRMWARE_CRED_SENTINEL) $(wildcard $(FIRMWARE_DIR)/*.ino)
	@echo "--- Building ESP32 firmware ${CYAN}$(FIRMWARE_BUILD)/can-bridge.ino.bin${NC} ---"
	@mkdir -p $(FIRMWARE_BUILD)
	@$(show_wifi)
	arduino-cli compile --fqbn $(FQBN) $(FIRMWARE_DIR) --output-dir $(FIRMWARE_BUILD) --build-property "compiler.cpp.extra_flags=$(FIRMWARE_CFLAGS) $(FIRMWARE_EXTRA_CFLAGS)"
	$(ESPTOOL) \
		--chip esp32 \
		$(ESPTOOL_MERGE_CMD) --output $(FIRMWARE_BUILD)/can-bridge.ino.merged.bin --target-offset 0x0 \
		0x1000 $(FIRMWARE_BUILD)/can-bridge.ino.bootloader.bin \
		0x8000 $(FIRMWARE_BUILD)/can-bridge.ino.partitions.bin \
		0x10000 $(FIRMWARE_BUILD)/can-bridge.ino.bin

firmware: test $(FIRMWARE_BUILD)/can-bridge.ino.bin

firmware-port:
	@if [ -z "$(ESP32_PORT)" ]; then printf "${RED}Error: no ESP32 serial port detected. Plug in the board. $(NC)Override with: make reboot-over-usb ESP32_PORT=$(PURPLE)/dev/cu.XXXX${NC}\r\n" >&2; exit 1; fi
	@printf "$(PURPLE)$(ESP32_PORT)$(NC)\r\n"

flash flash-usb firmware-flash: firmware-port firmware native test
	@echo "Flashing ${CYAN}$(FIRMWARE_BUILD)/can-bridge.ino.bin${NC} via $(ESP32_PORT)..."
	@$(show_wifi)
	$(ESPTOOL) --port "$(ESP32_PORT)" --baud 460800 write_flash 0x0 $(FIRMWARE_BUILD)/can-bridge.ino.merged.bin
	@echo "${GREEN}Flash complete. Reading startup log from $(ESP32_PORT) at 115200 baud...${NC}"
	@$(MAKE) startup-log ESP32_PORT="$(ESP32_PORT)"
	@echo "Startup log complete."

flash-over-usb: flash

startup-log: firmware-port
	@scripts/serial-startup-log.pl --port "$(ESP32_PORT)" --baud 115200 --max-wait 30 --post-byte 30


monitor: firmware-port

	@screen "$(ESP32_PORT)" 115200

reboot-over-usb: firmware-port
	@if [ -z "$(ESPTOOL)" ]; then echo "${RED}Error: esptool not found under $(HOME)/Library/Arduino15/packages/esp32/tools/esptool_py/${NC}" >&2; exit 1; fi
	@echo "Starting serial logger, then resetting ${PURPLE}$(ESP32_PORT)${NC} via esptool USB control reset..."
	@scripts/serial-startup-log.pl --port "$(ESP32_PORT)" --baud 115200 --max-wait 30 --post-byte 30 --reset-esptool --esptool "$(ESPTOOL)"

# -- ESP32 OTA (signed-image, Ed25519ph, over-HTTP) -------------------------
#
# make ota-keys    -- generate a per-user Ed25519 OTA signing keypair + bake
#                    the public key into firmware/can-bridge/OtaPublicKey.h
# make flash-over-tcp   -- sign the built firmware + push it to an ESP32 over WiFi
#
# The firmware is signed with Ed25519ph (RFC 8032 pre-hashed) before push.
# The ESP32 verifies the signature BEFORE committing. Tampered images are
# rejected and the running firmware is untouched.
#
# SECURITY NOTES
#   - Firmware authenticity: Ed25519ph signing prevents tampered images.
#   - Transport: HTTP (not HTTPS). Auth credentials and firmware travel in
#     cleartext on the wire. This is acceptable on a trusted local network
#     but does NOT provide transport-layer confidentiality. For untrusted
#     networks, use a VPN or consider HTTPS with a self-signed cert.
#     No defaults — fail-closed (empty creds = OTA always rejected).
#
# ESP32_HOST the ESP32 IP/hostname to push to (REQUIRED for flash-over-tcp / capture-tcp)
# OTA port for firmware push (default: 80)
# OTA_KEYS_DIR  per-user signing keypair dir (NEVER committed)
#
# NOTE: the FIRST time you change your keypair, the new public key must be
#       flashed over USB (make flash) so the device trusts it. Subsequent
#       updates can go over WiFi via flash-over-tcp.


OTA_KEYS_DIR ?= $(HOME)/.vehicle-sim/ota

# OTA credentials — generated by make ota-creds, persisted in ~/.zshrc
# Used for TCP auth token baked into firmware at build time
# Never hardcoded — always injected at build time.

# ── ESP32 reachability check ──────────────────────────────────────────────
# Usage: make check-esp32 ESP32_HOST=192.168.68.60
# Pings the device and reports whether it's reachable on the network.
check-esp32:
		echo "  make check-esp32 ESP32_HOST=<esp32-ip>" >&2; \
		exit 1; \
	fi
	@echo "Pinging $(_IP)..."
	@ping -c 3 -t 2 "$(_IP)" 2>/dev/null | tail -1
	@echo ""
	@echo "ARP table:"
	@arp -an | grep "$(_IP)" 2>/dev/null || echo "  (no ARP entry)"
	@echo ""
	@echo "TCP port 3333:"
	@nc -z -w 2 "$(_IP)" 3333 2>/dev/null && echo "  OPEN" || echo "  CLOSED/unreachable"
	@echo ""
	@echo "TCP port 80:"
	@nc -z -w 2 "$(_IP)" 80 2>/dev/null && echo "  OPEN" || echo "  CLOSED/unreachable"

# TCP auth token — single credential for TCP commands and OTA
# Generated by make ota-creds, persisted in ~/.zshrc, baked into firmware via TCP_AUTH_TOKEN
_TCP_GEN_TOKEN := $(shell openssl rand -hex 16)

ota-creds:
	@mkdir -p $(HOME)/.vehicle-sim; \
	echo ""; \
	echo "--- Generated TCP auth token ---"; \
	echo "    ESP32_TCP_TOKEN=$(_TCP_GEN_TOKEN)"; \
	echo ""; \
	printf "Persist to ~/.zshrc so this machine always uses these? [Y/n] "; \
	read _answer; \
	if [ "$$_answer" != "n" ] && [ "$$_answer" != "N" ]; then \
		if [ -f $(HOME)/.zshrc ] && grep -q 'export ESP32_TCP_TOKEN=' $(HOME)/.zshrc; then \
			sed -i '' 's/^export ESP32_TCP_TOKEN=.*/export ESP32_TCP_TOKEN="$(_TCP_GEN_TOKEN)"/' $(HOME)/.zshrc; \
			echo "    ${GREEN}Updated ESP32_TCP_TOKEN in ~/.zshrc${NC}"; \
		else \
			echo 'export ESP32_TCP_TOKEN="$(_TCP_GEN_TOKEN)"' >> $(HOME)/.zshrc; \
			echo "    ${GREEN}Written ESP32_TCP_TOKEN to ~/.zshrc${NC}"; \
		fi; \
		echo "    Run 'source ~/.zshrc' or open a new terminal to use them."; \
	else \
		echo "    Set manually:"; \
		echo "    ${YELLOW}export ESP32_TCP_TOKEN=$(_TCP_GEN_TOKEN)${NC}"; \
	fi; \
	echo ""

ota-keys:
	@echo "--- Generating per-user OTA signing keypair ---"
	@scripts/ota-generate-keys.sh --keys-dir "$(OTA_KEYS_DIR)"
	@echo ""
	@echo "IMPORTANT: the public key was baked into firmware/can-bridge/OtaPublicKey.h."
	@echo "The FIRST time you use this keypair you MUST re-flash over USB so the"
	@echo "device trusts it:  make flash"
	@echo "Subsequent updates can be pushed over WiFi:  make flash-over-tcp ESP32_HOST=<ip>"

discover: native
	@./build-native/vehicle-sim --discover


flash-wifi flash-over-wifi flash-tcp flash-over-tcp: firmware native
	@if [ -z "$(ESP32_HOST)" ]; then \
		echo "${YELLOW}WARN: ESP32_HOST not set. Attempting auto-discovery...${NC}"; \
		echo "      (This will fail if the ESP32 is in AP mode or on a different network.)"; \
		echo "      Set ESP32_HOST=<ip> to skip discovery."; \
		DISCOVERED_IP=$$(./build-native/vehicle-sim --discover 2>/dev/null | \
			grep -oE 'tcp:[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+' | head -1 | cut -d: -f2); \
		if [ -z "$$DISCOVERED_IP" ]; then \
			echo "${RED}Error: could not auto-discover ESP32.${NC}" >&2; \
			echo "  Is the ESP32 on the same network? Try: make flash-over-tcp ESP32_HOST=<ip>" >&2; \
			exit 1; \
		fi; \
		echo "${GREEN}--- Found ESP32 at $$DISCOVERED_IP ---${NC}"; \
		ESP32_HOST="$$DISCOVERED_IP"; \
	fi
	@echo "--- Signing firmware ---"
	@scripts/ota-sign.sh $(FIRMWARE_BUILD)/can-bridge.ino.bin --keys-dir "$(OTA_KEYS_DIR)"
	@echo "--- Pushing signed firmware to $(ESP32_HOST):80 ---"
	@python3 scripts/ota-push.py $(FIRMWARE_BUILD)/can-bridge.ino.bin \
		--host "$(ESP32_HOST)" --port 80 \
		--keys-dir "$(OTA_KEYS_DIR)"

reboot-tcp reboot-wifi reboot-over-wifi reboot-over-tcp:
	@if [ -z "$(ESP32_HOST)" ]; then \
		echo "Error: ESP32_HOST is required." >&2; \
		echo "  make reboot-over-tcp ESP32_HOST=<esp32-ip>" >&2; \
		exit 1; \
	fi
	@echo "Rebooting $(ESP32_HOST):3333..."
	@printf 'AUTH $(ESP32_TCP_TOKEN)\rATZ\rATE0\rATREBOOT\r' | nc -w 5 "$(ESP32_HOST)" 3333 2>/dev/null; \
	_rc=$$?; \
	if [ $$_rc -ne 0 ]; then \
		echo "${YELLOW}WARN: no response (device may have already rebooted)${NC}"; \
	fi

# -- SonarCloud (ESP32 firmware — code quality only, no coverage) ----------
#
# make sonar-scan     -- generate compile_commands.json + upload to SonarCloud
#                        (CE-polls for SUCCESS before caching the issue report)
# make sonar-summary  -- print the cached issue counts by severity
# make sonar-compiledb-- regenerate just the compilation database
# make sonar-clean    -- drop cached reports + scanner work dir
#
# CODE QUALITY ONLY (bugs, vulnerabilities, smells, complexity). Coverage is
# deferred until the firmware is broken into a host-testable library — there
# are no firmware unit tests today, so synthesising coverage would be noise.
#
# Requires SONAR_TOKEN (or SONAR_TOKEN_ES) in the environment, plus arduino-cli
# with the esp32:esp32 core installed. The compilation database captures the
# xtensa cross-compile flags (ESP-IDF includes, defines, -std=gnu++11); cfamily
# runs its own front end against them so the ESP32 toolchain is not invoked.
#
# Pattern mirrors engine-sim-bridge: token fallback, -D overrides, CE-poll gate.

SONAR_PROJECT_KEY   := danieljsinclair_vehicle-sim-esp32
SONAR_COMPILE_DB    := $(FIRMWARE_BUILD)/compiledb/compile_commands.json
# arduino-cli emits the sketch as a single preprocessed amalgam TU capturing
# the full xtensa cross-compile command (ESP-IDF includes/defines). The filter
# script reuses that command but emits one compile-commands entry PER .ino
# source (can-bridge.ino, ota_update.ino), forced to C++ via -x c++ so cfamily
# parses them. See scripts/sonar_filter_compdb.py for the five fixes applied.
SONAR_COMPILE_DB_F  := $(FIRMWARE_BUILD)/compile_commands.json
SONAR_REPORT        := $(FIRMWARE_BUILD)/sonar-report.json
SONAR_REMOVED_FACET := $(FIRMWARE_BUILD)/sonar-removed-facet.json
SONAR_SCANNER_LOG   := $(FIRMWARE_BUILD)/sonar-scanner.log

# Generate the compilation database from the ESP32 Arduino build without
# compiling, then build a cfamily-compatible per-.ino compile_commands.json.
sonar-compiledb: $(SONAR_COMPILE_DB_F)

$(SONAR_COMPILE_DB_F): $(wildcard $(FIRMWARE_DIR)/*.ino) $(wildcard $(FIRMWARE_DIR)/*.h) scripts/sonar_filter_compdb.py
	@echo "--- Generating ESP32 compilation database ${CYAN}$(SONAR_COMPILE_DB_F)${NC} ---"
	@mkdir -p $(FIRMWARE_BUILD)/compiledb
	@arduino-cli compile --fqbn $(FQBN) $(FIRMWARE_DIR) \
		--build-path $(FIRMWARE_BUILD)/compiledb \
		--only-compilation-database \
		--build-property "compiler.cpp.extra_flags=-DTCP_AUTH_TOKEN=\"$(ESP32_TCP_TOKEN)\"" \
		> /dev/null 2>$(FIRMWARE_BUILD)/compiledb/arduino-cli.log || { \
			echo "${RED}arduino-cli compile failed; see $(FIRMWARE_BUILD)/compiledb/arduino-cli.log${NC}"; \
			tail -n 20 $(FIRMWARE_BUILD)/compiledb/arduino-cli.log; exit 1; }
	@python3 scripts/sonar_filter_compdb.py $(SONAR_COMPILE_DB) $(SONAR_COMPILE_DB_F) "$(CURDIR)/$(FIRMWARE_DIR)"
	@echo "${GREEN}compile_commands.json ready${NC} (firmware amalgam TU, .cpp)"

# Upload to SonarCloud and poll the Compute Engine to SUCCESS before caching.
# `sonar-scan` is a phony alias; the actual gating is via the $(SONAR_REPORT)
# file target — only re-scans when compile_commands.json or sonar-project.properties
# actually change (dependency-gated, not phony).
sonar-scan: $(SONAR_REPORT)

$(SONAR_REPORT): $(SONAR_COMPILE_DB_F) sonar-project.properties
	@if [ -z "$${SONAR_TOKEN_ES}" ] && [ -z "$${SONAR_TOKEN}" ]; then \
		echo "  ${YELLOW}No SONAR_TOKEN — skipping scan (run 'source ~/.zshrc' to enable)${NC}"; exit 0; \
	fi
	@echo "=== [vehicle-sim-esp32] Running sonar-scanner (quality only) ==="
	@mkdir -p $(FIRMWARE_BUILD)
	@SONAR_TOKEN="$${SONAR_TOKEN_ES:-$${SONAR_TOKEN}}" sonar-scanner \
		> $(SONAR_SCANNER_LOG) 2>&1; \
		rc=$$?; \
		if [ $$rc -ne 0 ]; then \
			echo "${RED}=== [vehicle-sim-esp32] sonar-scanner failed (rc=$$rc); see $(SONAR_SCANNER_LOG) ===${NC}"; \
			tail -n 25 $(SONAR_SCANNER_LOG); exit $$rc; \
		fi
	@echo "=== [vehicle-sim-esp32] Waiting for SonarCloud Compute Engine to finish ==="
	@TOKEN="$${SONAR_TOKEN_ES:-$${SONAR_TOKEN}}"; \
		CETASKID=$$(grep -E '^ceTaskId=' .scannerwork/report-task.txt 2>/dev/null | cut -d= -f2); \
		if [ -z "$$CETASKID" ]; then \
			echo "${RED}ERROR: no ceTaskId in .scannerwork/report-task.txt; cannot confirm analysis settled${NC}"; exit 1; \
		fi; \
		echo "  CE task: $$CETASKID"; \
		dead=0; \
		while [ $$dead -lt 60 ]; do \
			status=$$(curl -s -u "$$TOKEN:" "https://sonarcloud.io/api/ce/task?id=$$CETASKID" \
				| python3 -c "import json,sys; print(json.load(sys.stdin).get('task',{}).get('status',''))" 2>/dev/null); \
			if [ "$$status" = "SUCCESS" ]; then \
				echo "${GREEN}  CE task SUCCESS after $$(($$dead * 2))s${NC}"; break; \
			fi; \
			if [ "$$status" = "FAILED" ] || [ "$$status" = "CANCELED" ]; then \
				echo "${RED}ERROR: SonarCloud CE task $$status (id=$$CETASKID); report did not settle${NC}"; exit 1; \
			fi; \
			sleep 2; dead=$$((dead + 1)); \
		done; \
		if [ "$$status" != "SUCCESS" ]; then \
			echo "${RED}ERROR: timed out waiting for SonarCloud CE task (last status=$$status, id=$$CETASKID)${NC}"; exit 1; \
		fi
	@echo "=== [vehicle-sim-esp32] Caching SonarCloud issue report ==="
	@TOKEN="$${SONAR_TOKEN_ES:-$${SONAR_TOKEN}}"; \
		curl -s -u "$$TOKEN:" "https://sonarcloud.io/api/issues/search?componentKeys=$(SONAR_PROJECT_KEY)&ps=500&statuses=OPEN" \
			> $(SONAR_REPORT) 2>/dev/null || true
	@$(MAKE) --no-print-directory sonar-summary

# Print issue counts by severity + top rules from the cached report (or live).
sonar-summary:
	@TOKEN="$${SONAR_TOKEN_ES:-$${SONAR_TOKEN}}"; \
	if [ -n "$$TOKEN" ]; then \
		curl -s -u "$$TOKEN:" "https://sonarcloud.io/api/issues/search?componentKeys=$(SONAR_PROJECT_KEY)&ps=500&statuses=OPEN&facets=impactSeverities" > $(SONAR_REPORT) 2>/dev/null || true; \
	fi
	@if [ ! -f "$(SONAR_REPORT)" ]; then \
		if [ -z "$${SONAR_TOKEN_ES}" ] && [ -z "$${SONAR_TOKEN}" ]; then \
			echo "  No token and no cached report; run: source ~/.zshrc"; exit 0; \
		fi; \
	fi
	@python3 scripts/sonar_summary.py $(SONAR_REPORT) --label "[vehicle-sim-esp32]"

# -- End-of-make headline (ONE compact coloured line) ----------------------
#
# The end-of-make summary. Emits ONE scannable line for vehicle-sim:
#
#     [vehicle-sim] tests: PASS 836/836 | sonar: open 41 / total 41 (open-only)
#
# Fields with no data are OMITTED gracefully (no coverage source yet -- the
# ESP32 firmware has no unit tests, so coverage is deferred; the cov field
# therefore omits until a --cov-measures/--local-cov source is added).
# Contrast with `sonar-summary` above, which prints the FULL multi-line issue
# report. `summary` is what `all` ends with; `sonar-summary` is standalone.
#
# READY TO EXTEND: when the C++ core + iOS sonar projects land, add a SECOND
# build_summary.py invocation (different --label / --sonar-report) here, or a
# recursive `$(MAKE) -C <sub> summary`. Fixed column widths keep lines aligned.
summary:
	@python3 scripts/build_summary.py \
		--label "[vehicle-sim]" \
		--test-log "$(TEST_REPORT)" \
		--sonar-report "$(SONAR_REPORT)"
	@echo "HINT: run 'make sonar-summary' to see the full SonarCloud issue report (or live if SONAR_TOKEN is set)."
	@echo 

sonar-clean:
	@rm -f $(SONAR_REPORT) $(SONAR_REMOVED_FACET)
	@rm -rf .scannerwork

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

capture capture-usb capture-over-usb: test-cpp
	@if [ -z "$(ESP32_PORT)" ]; then echo "Error: no ESP32 serial port detected." >&2; exit 1; fi
	@echo "      ESP32_PORT: $(PURPLE)$(ESP32_PORT)$(NC)"
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
#   make capture-tcp                       -> captures/capture_<ts>.raw.txt + .csv
#   CAPFILE=TeslaMonday make capture-tcp    -> captures/TeslaMonday_<ts>.raw.txt + .csv
#
# Requires ESP32_HOST to be set (the ESP32 IP/hostname on the local network).

capture-tcp capture-over-tcp capture-wifi capture-over-wifi: native
	@if [ -z "$(ESP32_HOST)" ]; then \
		echo "${RED}Error: ESP32_HOST is required.${NC}" >&2; \
		echo "  make capture-tcp ESP32_HOST=<esp32-ip>" >&2; \
		exit 1; \
	fi
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
	@echo "  flash-over-tcp  - Sign + push firmware to ESP32 over WiFi (alias: flash-tcp; requires ESP32_HOST=<ip>)"
	@echo "  monitor          - Serial monitor at 115200 baud (live view)"
	@echo "  startup-log      - Wait up to 30s for startup bytes, then read for 30s"
	@echo "  reboot-over-usb  - Send ATREBOOT over USB serial, then print startup log"
	@echo "  reboot-over-tcp  - Send ATREBOOT over TCP port 3333 (requires ESP32_HOST=<esp32-ip>)"
	@echo "  capture          - Log CAN frames via USB (aliases: capture-usb)"
	@echo "  capture-tcp      - Log CAN frames over WiFi TCP (alias: capture-tcp; requires ESP32_HOST=<esp32-ip>)"
	@echo "  firmware-port    - Show detected ESP32 serial port"
	@echo "  ota-keys         - Generate per-user Ed25519 OTA signing keypair + bake public key"
	@echo "  sonar-scan       - Run SonarCloud static analysis on firmware (quality only; needs SONAR_TOKEN)"
	@echo "  sonar-summary    - Print cached SonarCloud issue counts by severity (FULL report)"
	@echo "  summary          - Print ONE-LINE end-of-make headline (tests + sonar); end of 'all'"
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

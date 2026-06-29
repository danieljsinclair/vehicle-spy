.PHONY: all clean test test-cpp help ios ios-signed xcode native deploy deploy-app deploy-ios run run-app run-ios \
	        install-deps ios-icons app-icons scrub update-dbc firmware-wifi-sentinel \
	        firmware firmware-flash flash flash-usb monitor firmware-port capture capture-usb startup-log firmware-clean \
	        capture-tcp ota-keys flash-over-tcp flash-over-usb reboot-over-usb reboot-over-tcp check-esp32 \
	        sonar-scan sonar-scan-ios sonar-scan-esp32 sonar-summary sonar-compiledb sonar-compiledb-cpp sonar-compiledb-merge sonar-clean summary \
	        coverage-run coverage-clean coverage-summary \
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

# Default -- build + test all platforms + coverage + THREE sonar scans + headline.
# `sonar-scan` (vehicle-spy = C++ core), `sonar-scan-ios` (vehicle-spy-ios = iOS
# app), and `sonar-scan-esp32` (vehicle-spy-esp32 = ESP32) are each
# dependency-gated: a scan runs only when THAT project's inputs change.
# `summary` (the THREE-LINE headline, one per project) is ALWAYS the last output.
all: header test firmware ios coverage-run coverage-ios sonar-scan sonar-scan-ios sonar-scan-esp32 sonar-summary summary

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
	rm -rf build-native build-ios build-cov build-sonar $(FIRMWARE_BUILD)
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
$(warning Set: export ESP32_WIFI_SSID=yourSSID ESP32_WIFI_PASS=yourpassword)
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
		echo "  ${YELLOW}WiFi credentials changed${NC} -> ${GREEN}rebuilding firmware...${NC}"; \
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

# -- SonarCloud (THREE projects) ------------------------------------------
#
# The codebase is split into THREE SonarCloud projects (mirroring the code
# structure), each with its own properties file, scan target, and cached
# reports:
#
#   vehicle-spy          (sonar-project.properties)         -- THIS file
#       C++ core (src/, include/) + unit tests (test/). Quality (cfamily reads
#       build-cov/compile_commands.json) + coverage (C++ lcov from CMake tests).
#       Reports cache under build-sonar/.
#
#   vehicle-spy-ios      (sonar-project-ios.properties)
#       iOS app (vehicle-sim-ios/VehicleSim/) only. Swift + Obj-C++ bridging
#       layer. Quality (SonarCloud's native Swift/Obj-C plugin) + coverage
#       (iOS xccov from xcodebuild test -enableCodeCoverage). No compile_commands
#       needed (Xcode build log is the source). Reports cache under build-ios/.
#
#   vehicle-spy-esp32 (sonar-project-esp32.properties)
#       ESP32 firmware (firmware/can-bridge/) only. Quality only (no coverage:
#       no firmware unit tests). cfamily reads build-firmware/compile_commands
#       (Arduino/xtensa, filtered by sonar_filter_compdb.py).
#       Reports cache under build-firmware/.
#
# make coverage-run          -- build-cov (llvm-cov instr) + run C++ tests + lcov/XML
# make coverage-ios          -- xcodebuild test -enableCodeCoverage + xccov/XML
# make sonar-compiledb       -- ESP32 (arduino-cli) compile_commands only
# make sonar-compiledb-cpp   -- C++ (CMake) compile_commands only (build-cov)
# make sonar-compiledb-merge -- merge C++ + ESP32 DBs -> build-sonar/ (legacy; unused by scans)
# make sonar-scan            -- upload vehicle-spy (C++/iOS) to SonarCloud
# make sonar-scan-esp32      -- upload vehicle-spy-esp32 (firmware) to SonarCloud
# make sonar-summary         -- print cached/live issue counts by severity (BOTH projects)
# make summary               -- TWO-LINE headline (one per project)
# make sonar-clean           -- drop cached reports + scanner work dir
#
# Pattern mirrors engine-sim-bridge: artefact-as-target, coverage-before-scan,
# CE-poll gate, token: SONAR_TOKEN_ES only (no fallback).

# -- Per-project SonarCloud variables --
# Each project's report file target uses target-specific vars (SS_PROPERTIES,
# SS_PROJECT_KEY, etc.) set by $(call).  These are the per-project path
# variables those target-specific vars expand to.
SONAR_PROJECT_KEY    := danieljsinclair_vehicle-spy
SONAR_PROPERTIES     := sonar-project.properties
SONAR_REPORT         := build-sonar/sonar-report.json
SONAR_REMOVED_FACET  := build-sonar/sonar-removed-facet.json
SONAR_MEASURES       := build-sonar/sonar-measures.json
SONAR_SCANNER_LOG    := build-sonar/sonar-scanner.log

SONAR_IOS_PROJECT_KEY  := danieljsinclair_vehicle-spy-ios
SONAR_IOS_PROPERTIES   := sonar-project-ios.properties
SONAR_IOS_REPORT       := build-ios/sonar-report.json
SONAR_IOS_REMOVED_FACET := build-ios/sonar-removed-facet.json
SONAR_IOS_MEASURES     := build-ios/sonar-measures.json
SONAR_IOS_SCANNER_LOG  := build-ios/sonar-scanner.log

SONAR_ESP32_PROJECT_KEY  := danieljsinclair_vehicle-spy-esp32
SONAR_ESP32_PROPERTIES   := sonar-project-esp32.properties
SONAR_ESP32_REPORT       := build-firmware/sonar-report.json
SONAR_ESP32_REMOVED_FACET := build-firmware/sonar-removed-facet.json
SONAR_ESP32_MEASURES     := build-firmware/sonar-measures.json
SONAR_ESP32_SCANNER_LOG  := build-firmware/sonar-scanner.log

# == LLVM coverage tooling (xcrun with Homebrew fallback) ==
LLVM_COV      := $(shell xcrun --find llvm-cov 2>/dev/null || which llvm-cov 2>/dev/null)
LLVM_PROFDATA := $(shell xcrun --find llvm-profdata 2>/dev/null || which llvm-profdata 2>/dev/null)

# == C++ core coverage build (build-cov) ==
BUILD_COV_DIR       ?= build-cov
BUILD_COV_STAMP     := $(BUILD_COV_DIR)/.build-cov-ready.stamp
COVERAGE_XML_CPP    := $(BUILD_COV_DIR)/coverage-sonar.xml
COVERAGE_LCOV       := $(BUILD_COV_DIR)/lcov.info
# Source inputs that invalidate the coverage build when they change.
COV_BUILD_INPUTS    := $(shell find CMakeLists.txt test include src -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.cmake' -o -name '*.mm' \) 2>/dev/null | sort)

# Separate build dir with its OWN CMakeCache so coverage instrumentation does
# NOT invalidate the regular test build (build-native). RelWithDebInfo + llvm-cov
# flags (NOT Debug -- the latter is 3-5x slower and offers no coverage benefit).
$(BUILD_COV_DIR)/CMakeCache.txt: CMakeLists.txt
	@mkdir -p $(BUILD_COV_DIR)
	@cd $(BUILD_COV_DIR) && cmake \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping -g" \
		-DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate" \
		-DBUILD_IOS=OFF -DBUILD_TESTS=ON ..

$(BUILD_COV_STAMP): $(COV_BUILD_INPUTS) $(BUILD_COV_DIR)/CMakeCache.txt
	@echo "=== [vehicle-spy] Building coverage ($(BUILD_COV_DIR), RelWithDebInfo+instr) ==="
	@cmake --build $(BUILD_COV_DIR) --target vehicle-sim-lib vehicle-sim-tests --parallel
	@touch $@

# coverage-run: run the C++ tests under llvm-cov, merge profdata, export lcov,
# convert to SonarCloud generic XML. File-artefact target: re-runs only when
# the build, sources, or coverage script change. run_coverage_tests.sh writes
# coverage-sonar.xml itself.
$(COVERAGE_LCOV): $(BUILD_COV_STAMP) $(COV_BUILD_INPUTS) scripts/run_coverage_tests.sh
	@LLVM_PROFDATA="$(LLVM_PROFDATA)" LLVM_COV="$(LLVM_COV)" \
		bash scripts/run_coverage_tests.sh $(BUILD_COV_DIR)

coverage-run: $(COVERAGE_LCOV)
coverage-clean:
	@rm -f $(COVERAGE_LCOV) $(COVERAGE_XML_CPP) $(BUILD_COV_DIR)/coverage.profdata $(BUILD_COV_DIR)/coverage.txt
	@rm -rf $(BUILD_COV_DIR)/profraw

# == iOS coverage (xccov) ==
BUILD_IOS_DIR     := build-ios
IOS_SIMULATOR     := iPhone 16
COVERAGE_JSON_IOS := $(BUILD_IOS_DIR)/coverage.json
COVERAGE_XML_IOS  := $(BUILD_IOS_DIR)/coverage-ios.xml
IOS_COV_INPUTS    := $(shell find vehicle-sim-ios/VehicleSim vehicle-sim-ios/VehicleSimTests -type f \( -name '*.swift' -o -name '*.mm' -o -name '*.h' \) 2>/dev/null | sort) Makefile

# test-ios: run the iOS unit-test bundle on the simulator (no coverage).
# Reports pass/fail but does NOT gate the pipeline (some pre-existing tests
# are latent app bugs -- see commit history). The xcresult is still produced.
#
# xcodebuild prints results to stderr; we tee the full output to a log file
# (so failures are captured for post-mortem) then grep the test summary from
# the log for a concise on-screen verdict. The summary line looks like:
#   Test Suite 'VehicleSimTests.xctest' passed at ... :
#   Executed 51 tests, with 5 failures ...
# or
#   ** TEST FAILED **
IOS_TEST_LOG := $(BUILD_IOS_DIR)/ios-test.log

# test-ios: run the iOS unit-test bundle on the simulator (no coverage).
# File-artefact target: re-runs only when the log is missing or the app
# sources / Makefile have changed since the last run. The log is the
# artefact Make tracks; the coloured PASSED/FAILED line is just display.
test-ios: $(IOS_TEST_LOG)

$(IOS_TEST_LOG): app-icons $(IOS_COV_INPUTS)
	@echo "=== [vehicle-spy] Running iOS tests on simulator $(IOS_SIMULATOR) ==="
	@_run=$$(date +%Y%m%d-%H%M%S); \
	xcodebuild test \
		-project vehicle-sim-ios/VehicleSim/VehicleSimApp.xcodeproj \
		-scheme VehicleSimApp -configuration Debug \
		-destination 'platform=iOS Simulator,name=$(IOS_SIMULATOR),arch=arm64' \
		-derivedDataPath $(BUILD_IOS_DIR) \
		CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO \
		> $@ 2>&1 || true
	@_line=$$(grep -E 'Executed [0-9]+ tests' $@ | tail -1 | sed 's/^/  /'); \
	if grep -q 'Test Suite.*passed' $@ 2>/dev/null; then \
		echo "  $(GREEN)iOS tests: PASSED — $$_line$(NC)"; \
	elif grep -q 'Test Suite.*failed' $@ 2>/dev/null; then \
		echo "  $(RED)iOS tests: FAILED — $$_line$(NC)"; \
	else \
		echo "  $(YELLOW)iOS tests: unknown (see $@)$(NC)"; fi

# coverage-ios: xcodebuild test -enableCodeCoverage, extract xccov, convert to
# SonarCloud XML. Tolerates test FAILURES and hard crashes: writes the xcresult
# to a TIMESTAMPED path (so a crash mid-run doesn't clobber a prior good bundle)
# and, if no readable bundle can be extracted, emits an EMPTY coverage XML +
# warning rather than failing -- the scan then proceeds with C++ coverage only.
# (A deterministic app crash -- e.g. misaligned-pointer in DiscoveryPacket.parse
# -- currently prevents iOS coverage; fixing that is a separate product item.)
IOS_XCRESULT_DIR := $(BUILD_IOS_DIR)/xcresults
$(COVERAGE_XML_IOS): $(IOS_COV_INPUTS) scripts/xccov_to_sonar.py sonar-project.properties
	@echo "=== [vehicle-spy] Running iOS tests with coverage ==="
	@mkdir -p $(IOS_XCRESULT_DIR)
	@_run=$$(date +%Y%m%d-%H%M%S); \
	xcodebuild test \
		-project vehicle-sim-ios/VehicleSim/VehicleSimApp.xcodeproj \
		-scheme VehicleSimApp -configuration Debug \
		-destination 'platform=iOS Simulator,name=$(IOS_SIMULATOR),arch=arm64' \
		-enableCodeCoverage YES \
		-derivedDataPath $(BUILD_IOS_DIR) \
		-resultBundlePath $(IOS_XCRESULT_DIR)/run-$$_run.xcresult \
		CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO \
		> $(IOS_TEST_LOG) 2>&1 || true
	@_line=$$(grep -E 'Executed [0-9]+ tests' $(IOS_TEST_LOG) | tail -1 | sed 's/^/  /'); \
	if grep -q 'Test Suite.*passed' $(IOS_TEST_LOG) 2>/dev/null; then \
		echo "  $(GREEN)iOS tests: PASSED — $$_line$(NC)"; \
	elif grep -q 'Test Suite.*failed' $(IOS_TEST_LOG) 2>/dev/null; then \
		echo "  $(RED)iOS tests: FAILED — $$_line$(NC)"; \
	else \
		echo "  $(YELLOW)iOS tests: unknown (see $(IOS_TEST_LOG))$(NC)"; fi
	@echo "=== [vehicle-spy] Extracting xccov coverage ==="
	@# Try bundles newest-first; a hard crash can leave the newest incomplete.
	@_got=0; \
	for b in $$(find $(IOS_XCRESULT_DIR) -name '*.xcresult' -type d 2>/dev/null | sort -r); do \
		if xcrun xccov view --report --json "$$b" > $(COVERAGE_JSON_IOS) 2>/dev/null \
			&& [ -s $(COVERAGE_JSON_IOS) ]; then \
			echo "  using: $$b"; _got=1; break; \
		fi; \
	done; \
	if [ "$$_got" != "1" ]; then \
		echo "${YELLOW}WARNING: no readable xcresult bundle (iOS tests likely crashed -- a latent app bug);${NC}"; \
		echo "${YELLOW}         emitting empty iOS coverage. C++ coverage + quality still upload.${NC}"; \
		printf '<?xml version="1.0" encoding="UTF-8"?>\n<coverage version="1"/>\n' > $(COVERAGE_XML_IOS); \
		printf '' > $(COVERAGE_JSON_IOS); \
	fi
	@if [ -s $(COVERAGE_JSON_IOS) ]; then \
		python3 scripts/xccov_to_sonar.py \
			--input $(COVERAGE_JSON_IOS) \
			--output $(COVERAGE_XML_IOS) \
			--project-root $(CURDIR) \
			--exclude-targets VehicleSimTests.xctest; \
	fi

coverage-ios: $(COVERAGE_XML_IOS)

# == ESP32 firmware compile database (arduino-cli, xtensa) ==
# Used by the vehicle-spy-esp32 project only. The vehicle-spy project
# reads build-cov/compile_commands.json (C++ CMake) directly -- no Arduino DB.
SONAR_COMPILE_DB_FW_RAW := $(FIRMWARE_BUILD)/compiledb/compile_commands.json
# arduino-cli emits the sketch as a single preprocessed amalgam TU capturing
# the full xtensa cross-compile command (ESP-IDF includes/defines). The filter
# script reuses that command but emits one compile-commands entry PER .ino
# source, forced to C++ via -x c++ so cfamily parses them. See
# scripts/sonar_filter_compdb.py for the five fixes applied.
SONAR_COMPILE_DB_FW  := $(FIRMWARE_BUILD)/compile_commands.json

# Generate the ESP32 compilation database without compiling, then filter.
sonar-compiledb: $(SONAR_COMPILE_DB_FW)

$(SONAR_COMPILE_DB_FW): $(wildcard $(FIRMWARE_DIR)/*.ino) $(wildcard $(FIRMWARE_DIR)/*.h) scripts/sonar_filter_compdb.py
	@echo "--- Generating ESP32 compilation database ${CYAN}$(SONAR_COMPILE_DB_FW)${NC} ---"
	@mkdir -p $(FIRMWARE_BUILD)/compiledb
	@arduino-cli compile --fqbn $(FQBN) $(FIRMWARE_DIR) \
		--build-path $(FIRMWARE_BUILD)/compiledb \
		--only-compilation-database \
		--build-property "compiler.cpp.extra_flags=-DTCP_AUTH_TOKEN=\"$(ESP32_TCP_TOKEN)\"" \
		> /dev/null 2>$(FIRMWARE_BUILD)/compiledb/arduino-cli.log || { \
			echo "${RED}arduino-cli compile failed; see $(FIRMWARE_BUILD)/compiledb/arduino-cli.log${NC}"; \
			tail -n 20 $(FIRMWARE_BUILD)/compiledb/arduino-cli.log; exit 1; }
	@python3 scripts/sonar_filter_compdb.py $(SONAR_COMPILE_DB_FW_RAW) $(SONAR_COMPILE_DB_FW) "$(CURDIR)/$(FIRMWARE_DIR)"
	@echo "${GREEN}ESP32 compile_commands.json ready${NC} (firmware amalgam TU, .cpp)"

# C++ (CMake) compile database -- produced by CMake in build-cov/ during the
# coverage build. Exposed as a named target so `make sonar-compiledb-cpp` works;
# the file is created as a side effect of $(BUILD_COV_DIR)/CMakeCache.txt.
sonar-compiledb-cpp: $(BUILD_COV_DIR)/CMakeCache.txt
	@test -f $(BUILD_COV_DIR)/compile_commands.json

# == Merged compile database (legacy; unused by the split scans) ==
# Historically combined the C++ CMake DB (build-cov) and the ESP32 arduino DB
# (build-firmware) into build-sonar/compile_commands.json for the single
# vehicle-spy project. Retained for reference/backwards-compat but neither
# scan target depends on it now: vehicle-spy reads build-cov directly,
# vehicle-spy-esp32 reads build-firmware directly.
SONAR_COMPILE_DB_MERGED := build-sonar/compile_commands.json

sonar-compiledb-merge: $(SONAR_COMPILE_DB_MERGED)

$(SONAR_COMPILE_DB_MERGED): $(BUILD_COV_DIR)/compile_commands.json $(SONAR_COMPILE_DB_FW) scripts/merge_compile_commands.py
	@python3 scripts/merge_compile_commands.py $@ \
		$(BUILD_COV_DIR)/compile_commands.json $(SONAR_COMPILE_DB_FW)

# == Shared sonar-scanner runner (DRY for all three projects) ==
# Uploads to SonarCloud using the properties file named by
# $(SS_PROPERTIES), CE-polls to SUCCESS, then caches the issue + measures
# reports. Token: SONAR_TOKEN_ES only (no fallback — KISS).
# The actual rule is a FUNCTION (via $(call)) so each scan target passes its
# own paths. Target-specific variables (SS_PROPERTIES, SS_PROJECT_KEY,
# SS_REPORT, SS_REMOVED_FACET, SS_MEASURES, SS_SCANNER_LOG, SS_LABEL,
# SS_COMPILE_DB, SS_COVERAGE_XML) are set by the caller BEFORE $(call).
define run_sonar_scan
	@if [ -z "$${SONAR_TOKEN_ES}" ]; then \
		echo "  ${RED}SONAR_TOKEN_ES not set — skipping $(SS_LABEL) scan${NC}"; exit 1; \
	fi
	@echo "=== [$(SS_LABEL)] Running sonar-scanner ==="
	@mkdir -p $$(dirname $(SS_SCANNER_LOG))
	@SONAR_TOKEN="$${SONAR_TOKEN_ES}" sonar-scanner \
		-Dproject.settings=$(SS_PROPERTIES) \
		> $(SS_SCANNER_LOG) 2>&1; \
		rc=$$?; \
		if [ $$rc -ne 0 ]; then \
			echo "${RED}=== [$(SS_LABEL)] sonar-scanner failed (rc=$$rc); see $(SS_SCANNER_LOG) ===${NC}"; \
			tail -n 25 $(SS_SCANNER_LOG); exit $$rc; \
		fi
	@echo "=== [$(SS_LABEL)] Waiting for SonarCloud Compute Engine to finish ==="
	@TOKEN="$${SONAR_TOKEN_ES}"; \
		CETASKID=$$(grep -E '^ceTaskId=' .scannerwork/report-task.txt 2>/dev/null | cut -d= -f2); \
		if [ -z "$$CETASKID" ]; then \
			echo "${RED}ERROR: [$(SS_LABEL)] no ceTaskId in .scannerwork/report-task.txt; cannot confirm analysis settled${NC}"; exit 1; \
		fi; \
		echo "  CE task: $$CETASKID"; \
		dead=0; \
		while [ $$dead -lt 60 ]; do \
			status=$$(curl -s -u "$$TOKEN:" "https://sonarcloud.io/api/ce/task?id=$$CETASKID" \
				| python3 -c "import json,sys; print(json.load(sys.stdin).get('task',{}).get('status',''))" 2>/dev/null); \
			if [ "$$status" = "SUCCESS" ]; then \
				echo "${GREEN}  [$(SS_LABEL)] CE task SUCCESS after $$(($$dead * 2))s${NC}"; break; \
			fi; \
			if [ "$$status" = "FAILED" ] || [ "$$status" = "CANCELED" ]; then \
				echo "${RED}ERROR: [$(SS_LABEL)] SonarCloud CE task $$status (id=$$CETASKID); report did not settle${NC}"; exit 1; \
			fi; \
			sleep 2; dead=$$((dead + 1)); \
		done; \
		if [ "$$status" != "SUCCESS" ]; then \
			echo "${RED}ERROR: [$(SS_LABEL)] timed out waiting for SonarCloud CE task (last status=$$status, id=$$CETASKID)${NC}"; exit 1; \
		fi
	@echo "=== [$(SS_LABEL)] Caching SonarCloud issue + measures reports ==="
	@TOKEN="$${SONAR_TOKEN_ES}"; \
		curl -s -u "$$TOKEN:" "https://sonarcloud.io/api/issues/search?componentKeys=$(SS_PROJECT_KEY)&ps=500&statuses=OPEN" \
			> $(SS_REPORT) 2>/dev/null || true; \
		curl -s -u "$$TOKEN:" "https://sonarcloud.io/api/measures/component?component=$(SS_PROJECT_KEY)&metricKeys=coverage,lines_to_cover,uncovered_lines" \
			> $(SS_MEASURES) 2>/dev/null || true
endef

# == SonarCloud scan targets (DRY via $(call)) ==
#
# Each scan has TWO targets:
#   1. A phony shorthand (sonar-scan) for convenience.
#   2. A FILE target ($(SONAR_REPORT)) that is gated on real artefacts —
#      coverage XMLs, compile databases, properties files.  Make only re-runs
#      the scan when one of those inputs actually changed.
#
# The per-project target-specific variable block is unavoidable in Make (each
# project has different paths), but the scanner invocation, CE-poll, and
# report-caching logic live in ONE place: $(run_sonar_scan).
#
# Each scan ALSO writes a summary-report file (build-*/sonar-summary.md) so
# `sonar-summary` can be a real file target (not phony) — it re-reads the
# cached JSON instead of hammering the SonarCloud API on every invocation.
# The file is touched when the summary is fresh, making `sonar-summary`
# skippable when nothing changed.

sonar-scan: $(SONAR_REPORT)

$(SONAR_REPORT): SS_PROPERTIES    := $(SONAR_PROPERTIES)
$(SONAR_REPORT): SS_PROJECT_KEY   := $(SONAR_PROJECT_KEY)
$(SONAR_REPORT): SS_REPORT        := $(SONAR_REPORT)
$(SONAR_REPORT): SS_REMOVED_FACET := $(SONAR_REMOVED_FACET)
$(SONAR_REPORT): SS_MEASURES      := $(SONAR_MEASURES)
$(SONAR_REPORT): SS_SCANNER_LOG   := $(SONAR_SCANNER_LOG)
$(SONAR_REPORT): SS_LABEL         := vehicle-spy
$(SONAR_REPORT): SS_COMPILE_DB    := $(BUILD_COV_DIR)/compile_commands.json

$(SONAR_REPORT): $(BUILD_COV_DIR)/compile_commands.json $(COVERAGE_XML_CPP) $(SONAR_PROPERTIES)
	$(run_sonar_scan)

sonar-scan-ios: $(SONAR_IOS_REPORT)

$(SONAR_IOS_REPORT): SS_PROPERTIES    := $(SONAR_IOS_PROPERTIES)
$(SONAR_IOS_REPORT): SS_PROJECT_KEY   := $(SONAR_IOS_PROJECT_KEY)
$(SONAR_IOS_REPORT): SS_REPORT        := $(SONAR_IOS_REPORT)
$(SONAR_IOS_REPORT): SS_REMOVED_FACET := $(SONAR_IOS_REMOVED_FACET)
$(SONAR_IOS_REPORT): SS_MEASURES      := $(SONAR_IOS_MEASURES)
$(SONAR_IOS_REPORT): SS_SCANNER_LOG   := $(SONAR_IOS_SCANNER_LOG)
$(SONAR_IOS_REPORT): SS_LABEL         := vehicle-spy-ios

$(SONAR_IOS_REPORT): $(COVERAGE_XML_IOS) $(SONAR_IOS_PROPERTIES)
	$(run_sonar_scan)

sonar-scan-esp32: $(SONAR_ESP32_REPORT)

$(SONAR_ESP32_REPORT): SS_PROPERTIES    := $(SONAR_ESP32_PROPERTIES)
$(SONAR_ESP32_REPORT): SS_PROJECT_KEY   := $(SONAR_ESP32_PROJECT_KEY)
$(SONAR_ESP32_REPORT): SS_REPORT        := $(SONAR_ESP32_REPORT)
$(SONAR_ESP32_REPORT): SS_REMOVED_FACET := $(SONAR_ESP32_REMOVED_FACET)
$(SONAR_ESP32_REPORT): SS_MEASURES      := $(SONAR_ESP32_MEASURES)
$(SONAR_ESP32_REPORT): SS_SCANNER_LOG   := $(SONAR_ESP32_SCANNER_LOG)
$(SONAR_ESP32_REPORT): SS_LABEL         := vehicle-spy-esp32
$(SONAR_ESP32_REPORT): SS_COMPILE_DB    := $(SONAR_COMPILE_DB_FW)

$(SONAR_ESP32_REPORT): $(SONAR_COMPILE_DB_FW) $(SONAR_ESP32_PROPERTIES)
	$(run_sonar_scan)

# sonar-summary: regenerate only when a report file or the summary script
# actually changed.  Reads the cached JSON (no live API call) so it's cheap
# and deterministic.  Each project's section is printed only if its report
# exists — partial scans still work.
sonar-summary: $(SONAR_REPORT) $(SONAR_IOS_REPORT) $(SONAR_ESP32_REPORT) scripts/sonar_summary.py
	@echo ""
	@if [ -f "$(SONAR_REPORT)" ]; then \
		echo "=== vehicle-spy (C++ core) ==="; \
		python3 scripts/sonar_summary.py $(SONAR_REPORT) --label "[vehicle-spy]" --removed-facet $(SONAR_REMOVED_FACET); \
		echo ""; \
	fi
	@if [ -f "$(SONAR_IOS_REPORT)" ]; then \
		echo "=== vehicle-spy-ios (iOS app) ==="; \
		python3 scripts/sonar_summary.py $(SONAR_IOS_REPORT) --label "[vehicle-spy-ios]" --removed-facet $(SONAR_IOS_REMOVED_FACET); \
		echo ""; \
	fi
	@if [ -f "$(SONAR_ESP32_REPORT)" ]; then \
		echo "=== vehicle-spy-esp32 (ESP32 firmware) ==="; \
		python3 scripts/sonar_summary.py $(SONAR_ESP32_REPORT) --label "[vehicle-spy-esp32]" --removed-facet $(SONAR_ESP32_REMOVED_FACET); \
		echo ""; \
	fi

# coverage-summary: print local coverage % (C++ lcov + iOS xccov). No prereq:
# this must NEVER trigger a scan/build. Graceful if files are absent.
coverage-summary:
	@echo ""
	@echo "=== [vehicle-spy] BEGIN: coverage summary ==="
	@if [ -f "$(COVERAGE_LCOV)" ]; then \
		echo "  C++ core (lcov):"; \
		python3 -c "import sys; sys.path.insert(0,'scripts'); from build_summary import _lcov_coverage; c=_lcov_coverage('$(COVERAGE_LCOV)'); print('    {:.1f}% ({}/{})'.format(c[2],c[0],c[1]) if c else '    n/a')" 2>/dev/null || echo "    n/a"; \
	else echo "  C++ core (lcov): n/a (run: make coverage-run)"; fi
	@if [ -f "$(COVERAGE_JSON_IOS)" ]; then \
		echo "  iOS app (xccov):"; \
		python3 -c "import sys; sys.path.insert(0,'scripts'); from build_summary import _xccov_coverage; c=_xccov_coverage('$(COVERAGE_JSON_IOS)'); print('    {:.1f}% ({}/{})'.format(c[2],c[0],c[1]) if c else '    n/a')" 2>/dev/null || echo "    n/a"; \
	else echo "  iOS app (xccov): n/a (run: make coverage-ios)"; fi
	@echo "=== [vehicle-spy] END: coverage summary ==="

# -- End-of-make headline (THREE compact coloured lines) --------------------
#
# The end-of-make summary. Emits THREE scannable lines, one per project:
#
#     [vehicle-spy]         tests: PASS 882/887 | cov: 70.4% 3868/5491 | sonar: open N / total M (no blocker)
#     [vehicle-spy-ios]     tests: PASS 30/35   | cov: 45.2% 623/1378 | sonar: open N / total M (no blocker)
#     [vehicle-spy-esp32] tests: N/A         | cov: N/A             | sonar: open N / total M (no blocker)
#
# tests  = C++ gtest (from test-report.txt) for vehicle-spy; iOS xcresult
#           ResultMetrics for vehicle-spy-ios; N/A for firmware.
# cov    = C++ lcov for vehicle-spy; iOS xccov for vehicle-spy-ios; N/A for
#           firmware. vehicle-spy does NOT include iOS coverage (that lives in
#           vehicle-spy-ios); the two must not double-count.
# sonar  = OPEN issues from each project's cached report (total = open + removed).
# Fields with no data are OMITTED gracefully. `summary` is what `all` ends with;
# `sonar-summary` is the FULL multi-line report.
#
# Dependency: re-runs only when a data source actually changed.  Each
# build_summary.py call reads its own inputs (test log, xcresult, lcov,
# sonar-measures JSON) — if none of those moved, make skips the recipe.
# The summary is written to a real file so Make tracks staleness; the
# file is what ``all`` consumes, not phony output.
SUMMARY_FILE := build-sonar/summary.txt

# summary: ALWAYS displays the 3-line headline (phony), regenerating the
# file only when inputs changed.  Separating display from generation means
# the headline prints even when nothing was rebuilt.
.PHONY: summary
summary: $(SUMMARY_FILE)
	@cat $(SUMMARY_FILE)

$(SUMMARY_FILE): $(SONAR_MEASURES) $(SONAR_IOS_MEASURES) $(SONAR_ESP32_MEASURES) \
		$(COVERAGE_LCOV) $(COVERAGE_JSON_IOS) \
		$(TEST_REPORT) \
		scripts/build_summary.py
	@mkdir -p $$(dirname $@)
	@_tmp=$$(mktemp build-sonar/.summary.XXXXXX); \
	python3 scripts/build_summary.py \
		--label "[vehicle-spy]" \
		--test-log "$(TEST_REPORT)" \
		--cov-measures "$(SONAR_MEASURES)" \
		--local-cov "$(COVERAGE_LCOV)" --local-type lcov \
		--sonar-report "$(SONAR_REPORT)" \
		--removed-facet "$(SONAR_REMOVED_FACET)" \
		> $$_tmp; \
	python3 scripts/build_summary.py \
		--label "[vehicle-spy-ios]" \
		--xcresult-glob "$(IOS_XCRESULT_DIR)/run-*.xcresult" \
		--cov-measures "$(SONAR_IOS_MEASURES)" \
		--local-cov "$(COVERAGE_JSON_IOS)" --local-type xccov \
		--sonar-report "$(SONAR_IOS_REPORT)" \
		--removed-facet "$(SONAR_IOS_REMOVED_FACET)" \
		>> $$_tmp; \
	python3 scripts/build_summary.py \
		--label "[vehicle-spy-esp32]" \
		--sonar-report "$(SONAR_ESP32_REPORT)" \
		--removed-facet "$(SONAR_ESP32_REMOVED_FACET)" \
		>> $$_tmp; \
	echo "HINT: run 'make sonar-summary' (full issues) or 'make coverage-summary' (coverage %)." >> $$_tmp; \
	mv $$_tmp $@

sonar-clean:
	@rm -f $(SONAR_REPORT) $(SONAR_REMOVED_FACET) $(SONAR_MEASURES)
	@rm -f $(SONAR_IOS_REPORT) $(SONAR_IOS_REMOVED_FACET) $(SONAR_IOS_MEASURES)
	@rm -f $(SONAR_ESP32_REPORT) $(SONAR_ESP32_REMOVED_FACET)
	@rm -rf .scannerwork build-sonar

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
	@echo "  coverage-run     - Build C++ core with llvm-cov instrumentation + run tests + lcov/XML"
	@echo "  coverage-ios     - Run iOS tests with xcodebuild -enableCodeCoverage + xccov/XML"
	@echo "  coverage-summary - Print local coverage % (C++ lcov + iOS xccov)"
	@echo "  test-ios         - Run iOS unit tests on the simulator (no coverage)"
	@echo "  sonar-scan        - Run SonarCloud analysis on vehicle-spy (C++ core, needs SONAR_TOKEN)"
	@echo "  sonar-scan-ios    - Run SonarCloud analysis on vehicle-spy-ios (iOS app, needs SONAR_TOKEN)"
	@echo "  sonar-scan-esp32  - Run SonarCloud analysis on vehicle-spy-esp32 (ESP32, needs SONAR_TOKEN)"
	@echo "  sonar-summary     - Print cached/live SonarCloud issue counts by severity for ALL THREE projects"
	@echo "  sonar-compiledb  - Regenerate ESP32 (arduino-cli) compilation database only"
	@echo "  sonar-compiledb-cpp - Ensure C++ (CMake) compilation database exists (build-cov)"
	@echo "  sonar-compiledb-merge - Merge C++ + ESP32 compile_commands -> build-sonar/ (legacy)"
	@echo "  summary          - Print TWO-LINE headline (one per project); end of 'all'"
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

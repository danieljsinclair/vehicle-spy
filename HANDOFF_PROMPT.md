# Handoff Prompt: Vehicle-Sim .ino → .cpp Refactor Continuation

## Context
You are taking over the `.ino` → `.cpp` extraction work for the ESP32 CAN bridge firmware (`firmware/can-bridge/can-bridge.ino`). The goal: extract inline `.ino` logic into tested, vanilla C++ components in `firmware/vanilla/`, wire them via `FirmwareApp`, and eliminate the parallel inline implementation. Sonar-zero on vehicle-spy, then vehicle-spy-esp32.

## Current State (as of commit ac093d3)

### Completed Extraction Stages
| Stage | Commit | What | `.ino` Lines |
|-------|--------|------|-------------|
| 1: CanBridge | `11c09c4` | Arduino adapters + CanBridge wiring | -59 net |
| 2: AtCommandDispatcher | `c74d199` | 11 AT handlers → vanilla dispatcher | 1560→1334 (-226) |
| 3: DiscoveryManager | `1302d5d` | Discovery logic → vanilla | 1334→1124 (-210) |

### Current `.ino` State
- **~1124 lines** (down from 1560)
- Remaining inline logic: WiFi state machine, OTA, StatusLED, NTP sync, TCP server loop
- Vanilla components ready: `WiFiManager`, `FirmwareApp`, `NtpTimeSync`, `OtaUpdateServer`, `StatusLEDRenderer`, `CanBridge` (all in `firmware/vanilla/`, tested)

### Vanilla Architecture
```
firmware/vanilla/
├── AtCommandDispatcher.{h,cpp}    # 11 AT handlers, DI interfaces
├── DiscoveryManager.{h,cpp}       # Broadcast/backoff/packet building
├── WiFiManager.{h,cpp}            # WiFi state machine + credentials
├── FirmwareApp.{h,cpp}            # Orchestrator owning all vanilla components
├── WiFiManager.{h,cpp}            # Credentials + state machine
├── NtpTimeSync.{h,cpp}            # NTP sync (deferred UDP)
├── OtaUpdateServer.{h,cpp}        # OTA HTTP server
├── StatusLEDRenderer.{h,cpp}      # LED patterns
├── CanBridge.{h,cpp}              # CAN frame handling
├── FirmwareApp.{h,cpp}            # Orchestrator
└── Interfaces (ITcpClientAt, ISerialAt, IEspAt, IWifiCredentialStore, IMonitorState, ICanDriver, ITcpClient, ISerialCan, IUdp, ITime, IDiscoverySigner, ITimeNtp)
```

### Test Coverage
- **Host tests**: 1058 pass (`make test`)
- **gmock suite**: 191 pass (`esp32-firmware-tests`, `make test` in firmware/)
- **iOS**: BUILD SUCCEEDED (`make ios`)
- **Firmware**: EXIT 0 (`make firmware`)

### Current Sonar State (vehicle-spy)
| Severity | Rule | Location | Description |
|----------|------|----------|-------------|
| HIGH | S5019 | TCPTransport.cpp:377 | Lambda capture |
| HIGH | S3776×2 | TCPTransport.cpp:367,570 | Cognitive complexity (41, 27) |
| HIGH | S3608 | TCPTransport.cpp:377 | Lambda capture |
| MEDIUM | S1188 | TCPTransport.cpp:377 | Lambda 33 lines |
| LOW | S6012×2 | TCPTransport.cpp:372,373 | Explicit template args |

**S6022**: Fixed in code, SonarCloud cache stale

## Next Stages (Priority Order)

### Stage 4: WiFiManager Wiring
**Goal**: Wire `WiFiManager` into `.ino`, replace inline WiFi state machine
- **Files to modify**: `can-bridge.ino`, `firmware/vanilla/WiFiManager.{h,cpp}`, `firmware/vanilla/FirmwareApp.{h,cpp}`
- **Adapters needed**: `ArduinoWiFi` (already exists, implements `IWiFi` + `IWiFiDiscovery`)
- **Inline code to remove**: WiFi state machine (`WiFiState::Context`, `applyStateTransition`, `updateWiFiStateMachine`, `shouldRetryWiFi`, `shouldFallbackToApMode`, `isInitialConnectTimeout`, `onWiFiDisconnected`), credential handling, NTP sync trigger
- **Verification**: Host tests + gmock + device flash/ping

### Stage 5: OTA/StatusLED/NTP Wiring
- Wire `OtaUpdateServer`, `StatusLEDRenderer`, `NtpTimeSync` via `FirmwareApp`
- Remove inline OTA HTTP server, LED patterns, NTP sync code

### Stage 6: TCP Server/Loop Cleanup
- Remove inline TCP server loop, client handling
- Thin `.ino` → `FirmwareApp` delegation only

## Build/Verification Commands
```bash
# Host tests (always run)
make test                    # 1058 tests, must pass

# iOS build (Xcode)
make ios                     # BUILD SUCCEEDED

# ESP32 firmware compile (requires xtensa toolchain)
make firmware                # EXIT 0, produces .bin

# Sonar scan
make sonar-clean && make coverage-run && make sonar-scan && make sonar-summary
```

## Key Files to Know
| File | Purpose |
|------|---------|
| `firmware/can-bridge/can-bridge.ino` | Main .ino (target for thinning) |
| `firmware/vanilla/FirmwareApp.{h,cpp}` | Main orchestrator |
| `firmware/vanilla/WiFiManager.{h,cpp}` | Next extraction target |
| `firmware/vanilla/AtCommandDispatcher.{h,cpp}` | Already wired |
| `firmware/vanilla/DiscoveryManager.{h,cpp}` | Already wired |
| `include/vehicle-sim/pipeline/TCPTransport.h/cpp` | Host TCP transport (not firmware) |
| `firmware/vanilla/FirmwareApp.cpp` | Where wiring happens |

## Testing Approach
1. **Host tests** (`make test`): All vanilla C++ logic
2. **gmock suite** (`cd firmware && cmake --build build --target esp32-firmware-tests`): Firmware logic with mocks
3. **Device flash**: `make firmware` → flash → ping `192.168.68.60` → serial monitor
4. **Sonar**: `make sonar-scan` → check `make sonar-summary`

## Constraints & Rules
- **NO sonar suppression**, **NO -Wno-error**, **NO NOSONAR**
- **One commit per rule group** (low-risk grouped, high-risk per instance)
- **S3776/S1188/S3608/S5019**: High-risk, DEAD LAST, full test coverage FIRST
- **S3776 is DEAD LAST** always
- **Test-first for high-risk**: Blind TDD → critic review → refactor
- **All 3 builds must pass** before commit: `make test`, `make ios`, `make firmware`
- **Use `make` targets only** - never hand-crafted compile lines
- **One builder at a time** - no concurrent edits

## Immediate Next Action
**Stage 4: Wire WiFiManager**
1. Examine `firmware/vanilla/WiFiManager.{h,cpp}` and `can-bridge.ino` WiFi state machine
2. Add Arduino adapters if needed (WiFiManager uses existing `ArduinoWiFi`)
3. Wire `WiFiManager` into `FirmwareApp` constructor
4. Replace inline WiFi state machine in `.ino` with `firmwareApp` delegation
6. Remove inline WiFi state machine code
7. Verify: `make test`, `make ios`, `make firmware` all green
7. Commit: `cpp: wire vanilla WiFiManager into can-bridge.ino (extraction stage 4)`

## Environment
- **Repo**: `/Users/danielsinclair/vscode/engine-sim-app/engine-sim-cli/vehicle-sim`
- **Device**: ESP32 USB, SSID `manht2` / pass `luckyshoe478`, IP `192.168.68.60`
- **No xtensa toolchain on this box** - `make firmware` won't run locally (user handles flash)
- **iOS build**: Works via Xcode on this Mac

## Stashed Changes
- `stash@{0}`: DiscoveryManager instrumentation (not part of sonar fixes)
- Do not pop unless needed

## S1448 Platform Coverage Gap
`BLEManagerMacOS.mm`/`BLEManageriOS.mm` have **zero test coverage** (not compiled in host test target). Need macOS test target with mocked CoreBluetooth or use iOS Xcode test infrastructure. Task assigned.

## S1188 Task
Task #7 assigned to test architect for blind TDD + critic review before S1188 refactor.

## Coverage Pipeline Issue
iOS coverage: 41.2% (SonarCloud) vs 26.2% (xccov) - 15% discrepancy. Task #6 assigned.

---

**You are now the lead. Run `make test`, `make ios`, `make firmware` to verify baseline. Then begin Stage 4: WiFiManager wiring. Report each stage completion with commit.**
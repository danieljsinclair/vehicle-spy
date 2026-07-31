# ESP32 S5421 (30 globals) — 6-Class Decomposition Shape

**Read-Only Analysis** for ESP32 CAN bridge firmware S5421 global variables reduction  
**Research Date:** 2025-01-06  
**Analyst:** Researcher Agent  
**Target Files:** `firmware/can-bridge/can-bridge.ino` (1498 lines, 30+ globals), `ota_update.ino` (355 lines, 5 globals)  
**User Mandate:** "Push hard for alternatives" — produce the holistic refactor shape (the "6-class refactor") for viewing before any implementation.

---

## Executive Summary

The ESP32 CAN bridge firmware has **30+ non-const global variables** across two `.ino` files (can-bridge.ino: ~25 globals, ota_update.ino: 5 globals). These globals span multiple concerns: WiFi state, NTP sync, UDP discovery, CAN bus, TCP server, LED status, OTA upload, AT command registry, and more.

**RECOMMENDED DECOMPOSITION:** **6 cohesive classes** that group related state and behavior:

1. **WiFiManager** — WiFi connection, state machine, credentials, retry logic
2. **DiscoveryManager** — UDP broadcast, packet building, signing, backoff timer
3. **NtpTimeSync** — SNTP initialization, callback, timestamp, retry logic
4. **CanBridge** — TWAI driver, frame streaming, monitor mode, TCP client
5. **AtCommandDispatcher** — Command registry, handlers, authentication, prompt sending
6. **OtaUpdateServer** — HTTP server, signature verification, upload state machine

**Additional Supporting Components:**
- **StatusLED** (already extracted) — LED patterns, hardware abstraction
- **Constants namespace** (already extracted) — All magic numbers

**RISK:** Medium-High. This is a **significant architectural refactor** that touches all firmware subsystems. The ESP32 has **NO host runtime tests**, so behavioral preservation must rely on:
- ESP32 hardware testing
- Manual verification of each subsystem
- Careful state management during the transition

---

## Current Global Variable Inventory

### can-bridge.ino — ~25 globals

| Variable | Type | Purpose | Current Scope |
|----------|------|---------|--------------|
| `wifiCredentials` | `Preferences` | NVS storage for WiFi SSID/password | WiFi credential management |
| `wifiCtx` | `WiFiState::Context` | WiFi state machine state | WiFi connection lifecycle |
| `tcpServer` | `WiFiServer` | TCP server instance | TCP connection lifecycle |
| `client` | `WiFiClient` | Connected TCP client | Command/frame streaming |
| `monitorActive` | `bool` | CAN monitor mode flag | AT command state |
| `serialQuietUntilMs` | `uint32_t` | Serial quiet period end time | Serial output throttling |
| `ntpCtx` | `NtpState::Context` | NTP sync state | Time synchronization |
| `discoveryCtx` | `DiscoveryState::Context` | Discovery backoff state | UDP broadcast timing |
| `discoveryDeviceId` | `std::array<uint8_t, 16>` | Device ID from MAC | Discovery packet tagging |
| `lastDiscoveryBroadcast` | `uint32_t` | Last broadcast timestamp | Discovery backoff |
| `udpDiscovery` | `WiFiUDP` | UDP socket for discovery | UDP broadcast |
| `realWifi` | `RealWiFi` | Real WiFi interface | WiFi abstraction |
| `wifi` | `IWiFi&` | Reference to WiFi interface | WiFi abstraction (dispatch) |
| `statusLed` | `StatusLED` | LED status manager | Visual feedback |
| `ledOutput` | `HardwareStatusLEDOutput` | LED hardware output | LED GPIO abstraction |
| `tcpServer` | `WiFiServer` | TCP server instance | TCP connection management |
| `atCommandHandlers` | `std::vector<AtCommandHandler*>` | AT command registry | Command pattern registry |
| Color constants | `const char*` | ANSI color codes | Serial output formatting |
| NVS constants | `constexpr char*` | NVS namespace/key names | NVS storage |
| AP credentials | `constexpr const char*` | AP mode SSID/password | Fallback WiFi credentials |
| Timeout constants | `uint32_t` | Various timeout values | Timing constants (already in Constants namespace) |

### ota_update.ino — 5 globals

| Variable | Type | Purpose | Current Scope |
|----------|------|---------|--------------|
| `otaHttp` | `WebServer` | HTTP server instance | OTA update endpoint |
| `otaUpdater` | `HTTPUpdateServer` | HTTP update server | OTA upload handling |
| `sodiumReady` | `bool` | libsodium initialization flag | Crypto readiness |
| `otaUploadStartTime` | `uint32_t` | Upload start timestamp | Upload timeout detection |
| `otaSig` | `std::array<uint8_t, 64>` | Parsed signature buffer | Signature verification |
| `otaHasSig` | `bool` | Signature received flag | Upload validation |
| `otaErr` | `String` | Upload error message | Error reporting |

**Total: ~32 non-const globals across both files**

---

## Recommended 6-Class Decomposition

### Class 1: WiFiManager

**Responsibility:** WiFi connection lifecycle, state machine, credential management, retry logic, TCP server lifecycle.

**State to Own:**
- `WiFiState::Context wifiCtx` (state, retry timers, disconnect reason)
- `Preferences wifiCredentials` (NSS storage)
- `WiFiServer tcpServer` (restarted on IP change)
- `WiFiClient client` (moved to CanBridge — see Class 4)

**Functions to Move:**
- `hasStoredWifiCredentials()`
- `loadWifiCredentials()`
- `storeWifiCredentials()`
- `clearWifiCredentials()`
- `determineCredentialSource()`
- `shouldFallbackToApMode()`
- `isInitialConnectTimeout()`
- All WiFi state handlers: `DisconnectedStateHandler`, `ConnectingStateHandler`, `ReconnectingStateHandler`, `ConnectedStaStateHandler`, `ConnectedApStateHandler`
- `getStateHandler()` (dispatch table)
- `applyStateTransition()`
- `updateWiFiStateMachine()` → `WiFiManager::update()`
- `onWiFiDisconnected()` (event handler)
- `restartTcpServerIfNeeded()`

**Interface (Public API):**
```cpp
class WiFiManager {
public:
    explicit WiFiManager(IWiFi& wifiIfc);
    void update(uint32_t now);  // Call each loop iteration
    bool isClientConnected() const;
    WiFiClient& getClient();  // Access for CanBridge
    void onDisconnected(const WiFiEvent_t& event, const WiFiEventInfo_t& info);
    
private:
    IWiFi& wifi_;
    WiFiState::Context ctx_;
    Preferences nvs_;
    WiFiServer tcpServer_;
    
    // State handlers (private)
    std::unique_ptr<WiFiStateHandler> getCurrentHandler();
    // ... (rest of implementation)
};
```

**SRP/DRY Win:**
- **SRP:** Single responsibility — WiFi connection lifecycle only.
- **DRY:** Eliminates WiFi state globals; credentials logic encapsulated.

**Risk:**
- **Medium:** WiFi state machine is complex; careful testing needed for reconnection logic.
- **TCP client ownership:** Must coordinate with CanBridge for shared `client` access.

---

### Class 2: DiscoveryManager

**Responsibility:** UDP discovery broadcast, packet building, Ed25519 signing (optional), backoff scheduling.

**State to Own:**
- `DiscoveryState::Context discoveryCtx`
- `std::array<uint8_t, 16> discoveryDeviceId`
- `uint32_t lastDiscoveryBroadcast`
- `WiFiUDP udpDiscovery`

**Functions to Move:**
- `deviceMessageTag()`
- `printTagged()`
- `buildDiscoveryPacket()`
- `signDiscoveryPacket()` (guarded)
- `broadcastDiscovery()`
- `discoveryIntervalMs()`
- `resetDiscoveryBackoff()`

**Interface (Public API):**
```cpp
class DiscoveryManager {
public:
    explicit DiscoveryManager(WiFiUDP& udp);
    void init(const std::array<uint8_t, 16>& deviceId);
    void update(uint32_t now);  // Call each loop iteration
    void resetBackoff();
    
private:
    WiFiUDP& udp_;
    std::array<uint8_t, 16> deviceId_;
    DiscoveryState::Context ctx_;
    uint32_t lastBroadcast_;
    
    void buildPacket(uint8_t* packet);
    void signPacket(uint8_t* packet);  // Guarded
    bool shouldBroadcast() const;
};
```

**SRP/DRY Win:**
- **SRP:** Single responsibility — discovery broadcast only.
- **DRY:** Eliminates discovery globals; packet building/signing encapsulated.

**Risk:**
- **Low-Medium:** Discovery logic is well-contained; minimal dependencies.
- **Signing optional:** Guarded compilation (`VEHICLE_SIM_ENABLE_DISCOVERY_SIGNING`) must be preserved.

---

### Class 3: NtpTimeSync

**Responsibility:** SNTP initialization, sync callback, timestamp retrieval, retry logic.

**State to Own:**
- `NtpState::Context ntpCtx`

**Functions to Move:**
- `ntpSyncCallback()`
- `initNtpSync()`
- `getCurrentTimestamp()`

**Interface (Public API):**
```cpp
class NtpTimeSync {
public:
    NtpTimeSync();
    void init();  // Call when WiFi connects
    uint64_t getCurrentTimestamp() const;
    bool isSynced() const;
    
private:
    NtpState::Context ctx_;
    
    static void syncCallback(struct timeval* tv);  // C callback wrapper
    void handleSync(struct timeval* tv);  // Instance method
};
```

**SRP/DRY Win:**
- **SRP:** Single responsibility — NTP sync only.
- **DRY:** Eliminates NTP globals; retry logic encapsulated.

**Risk:**
- **Low:** NTP logic is straightforward; minimal state.

---

### Class 4: CanBridge

**Responsibility:** TWAI driver management, CAN frame streaming to Serial/TCP, monitor mode state.

**State to Own:**
- `bool monitorActive` (moved from globals)
- `uint32_t serialQuietUntilMs`

**Functions to Move:**
- `streamFrame()`
- TWAI initialization code (from `setup()`)

**Interface (Public API):**
```cpp
class CanBridge {
public:
    explicit CanBridge(WiFiClient& client);
    void init();  // TWAI driver install
    void update();  // Drain TWAI RX queue, stream frames
    void setMonitorActive(bool active);
    bool isMonitorActive() const;
    
private:
    WiFiClient& client_;  // Reference to WiFiManager's client
    bool monitorActive_;
    uint32_t serialQuietUntil_;
    
    void streamFrame(const twai_message_t& msg);
};
```

**SRP/DRY Win:**
- **SRP:** Single responsibility — CAN frame streaming only.
- **DRY:** Eliminates CAN-related globals (`monitorActive`, `serialQuietUntilMs`).

**Risk:**
- **Medium:** Depends on WiFiManager for `client` reference; lifecycle coupling.
- **TWAI driver:** Hardware-specific; requires ESP32 testing.

---

### Class 5: AtCommandDispatcher

**Responsibility:** AT command registry, handler dispatch, authentication, prompt sending.

**State to Own:**
- `std::vector<AtCommandHandler*> atCommandHandlers`

**Functions to Move:**
- `normalizeAtCommand()`
- `buildHeloResponse()`
- `parseSetWifiParams()`
- `isValidAuthToken()`
- `handleATCommand()`
- `handleAT()` / `handleSerialAT()`
- `drainSerialATCommands()`
- All `AtCommandHandler` subclasses (already classes, move registration inside)

**Interface (Public API):**
```cpp
class AtCommandDispatcher {
public:
    using PromptSender = std::function<void(const char*)>;
    
    AtCommandDispatcher();
    void handle(const String& cmd, PromptSender sendPrompt);
    
private:
    std::vector<std::unique_ptr<AtCommandHandler>> handlers_;
    
    void registerHandlers();
    String normalizeCommand(const String& cmd);
};
```

**SRP/DRY Win:**
- **SRP:** Single responsibility — AT command dispatch only.
- **DRY:** Eliminates `atCommandHandlers` global; handler registry encapsulated.

**Risk:**
- **Low-Medium:** Command pattern already well-structured; minimal changes needed.
- **Dependencies:** Needs access to WiFiManager (for ATSETWIFI) and CanBridge (for ATMA).

---

### Class 6: OtaUpdateServer

**Responsibility:** HTTP OTA server, signature verification, upload state machine.

**State to Own:**
- `WebServer otaHttp`
- `HTTPUpdateServer otaUpdater`
- `bool sodiumReady`
- `uint32_t otaUploadStartTime`
- `std::array<uint8_t, 64> otaSig`
- `bool otaHasSig`
- `String otaErr`

**Functions to Move:**
- All from `ota_update.ino`:
  - `hexToByte()`
  - `parseHexSig()`
  - `verifyPartition()`
  - `otaErrorMessage()`
  - `otaHttpCode()`
  - `otaSetup()` → `OtaUpdateServer::init()`
  - `otaLoop()` → `OtaUpdateServer::update()`
  - `otaMarkValidOnBoot()` → `OtaUpdateServer::markValidOnBoot()`

**Interface (Public API):**
```cpp
class OtaUpdateServer {
public:
    OtaUpdateServer();
    void init();  // Start HTTP server
    void update();  // Handle client (non-blocking)
    void markValidOnBoot();  // Cancel rollback
    
private:
    WebServer http_;
    HTTPUpdateServer updater_;
    bool sodiumReady_;
    uint32_t uploadStartTime_;
    std::array<uint8_t, 64> signature_;
    bool hasSignature_;
    String error_;
    
    // Upload handlers (lambdas become private methods)
    void handleUploadStart();
    void handleUploadWrite();
    void handleUploadEnd();
    void handleUploadAbort();
    
    bool verifyPartition(const esp_partition_t* part, uint32_t size);
    // ... (helper methods)
};
```

**SRP/DRY Win:**
- **SRP:** Single responsibility — OTA update only.
- **DRY:** Eliminates all OTA globals; upload state encapsulated.

**Risk:**
- **Medium-High:** OTA logic is complex (signature verification, rollback, WDT feeding).
- **Security:** Critical functionality; thorough testing required.

---

## Supporting Components (Already Extracted)

### StatusLED (Already a Class)

**Current State:** Already well-designed class with hardware abstraction.

**No Changes Needed.** Already follows SRP; uses composition (HardwareStatusLEDOutput).

### Constants Namespace (Already Extracted)

**Current State:** `Constants` namespace groups all magic numbers.

**No Changes Needed.** Already follows DRY; constants are properly scoped.

---

## Refactor Risk Assessment

### Risk 1: State Access Coordination

**Concern:** Multiple classes need shared access to `WiFiClient client` (WiFiManager owns it, but CanBridge and AtCommandDispatcher use it).

**Mitigation:**
- **Reference passing:** WiFiManager provides `getClient()` accessor.
- **Lifecycle management:** Ensure WiFiManager outlives dependent classes.
- **Null checks:** Validate client connectivity before use.

### Risk 2: Event Handler Integration

**Concern:** WiFi event handler (`onWiFiDisconnected`) updates WiFi state and LED status. After refactor, it must call `WiFiManager::onDisconnected()` and `StatusLED` methods.

**Mitigation:**
- Keep event handler in main sketch, delegate to classes.
- Test WiFi disconnection/reconnection scenarios.

### Risk 3: No Host Runtime Tests

**Concern:** ESP32 code has NO host runtime tests. Refactor must preserve behavior without unit test safety net.

**Mitigation:**
- **Manual testing plan:** Test each subsystem independently before integration.
- **Incremental refactor:** Introduce one class at a time, keep globals temporarily as fallback.
- **Hardware verification:** Requires ESP32 + CAN transceiver setup.

### Risk 4: Arduino .ino Compilation

**Concern:** Moving functions to classes may break `.ino` auto-prototype generation.

**Mitigation:**
- Keep class definitions in separate `.h`/`.cpp` files (not in `.ino`).
- Main `.ino` only contains `setup()`/`loop()` and class instantiation.
- Use `#include` for class headers.

---

## Implementation Priority

1. **Phase 1: Low-Risk Classes** (CanBridge, NtpTimeSync, DiscoveryManager)
   - Minimal dependencies
   - Self-contained logic
   - Easy to verify independently

2. **Phase 2: Medium-Risk Classes** (AtCommandDispatcher)
   - Has dependencies (WiFiManager, CanBridge)
   - Command pattern already structured

3. **Phase 3: High-Risk Classes** (WiFiManager, OtaUpdateServer)
   - WiFiManager is central; affects all other classes
   - OtaUpdateServer is security-critical

4. **Phase 4: Integration Testing**
   - Verify all subsystems work together
   - Test WiFi reconnection, discovery, CAN streaming, OTA updates

---

## Host Test Harness Need

**Current State:** ESP32 firmware has NO host runtime tests.

**After Refactor:**
- Each class should have **mockable interfaces** (IWiFi, IUDP, etc. already exist).
- **Test doubles:** MockWiFi, MockUDP for unit testing on host.
- **Behavioral tests:** Test state transitions, retry logic, timeout handling.
- **Integration tests:** Test class interactions (not possible on host).

**Recommendation:**
- Start with **manual ESP32 testing** (hardware verification).
- Plan for **future host test harness** using mocks (not blocking for refactor).

---

## Alternative: Namespace-Based Grouping

If full class extraction is too disruptive, consider **namespace-based grouping**:

```cpp
namespace WiFi {
    static WiFiState::Context ctx;
    static Preferences nvs;
    static WiFiServer server;
    
    void update(uint32_t now);
    // ... (functions as before, but namespaced)
}

namespace Discovery {
    static DiscoveryState::Context ctx;
    static std::array<uint8_t, 16> deviceId;
    
    void broadcast(uint32_t now);
    // ... (functions as before, but namespaced)
}
```

**Pros:**
- Less disruptive than full class extraction
- Keeps existing function signatures
- Reduces global namespace pollution

**Cons:**
- Still globals (just namespaced)
- Sonar S5421 still flags them
- Doesn't encapsulate state

**Not recommended** for S5421 compliance — classes provide better encapsulation.

---

## Conclusion

The ESP32 CAN bridge firmware's 30+ globals can be grouped into **6 cohesive classes**, each with a single responsibility:

1. **WiFiManager** — WiFi lifecycle (8 globals)
2. **DiscoveryManager** — UDP discovery (4 globals)
3. **NtpTimeSync** — NTP sync (1 global)
4. **CanBridge** — CAN streaming (2 globals)
5. **AtCommandDispatcher** — AT commands (1 global)
6. **OtaUpdateServer** — OTA updates (5 globals)

**StatusLED and Constants** are already well-designed and need no changes.

**Risk:** Medium-High due to no host tests and extensive ESP32 dependencies. Recommend **incremental refactor** starting with low-risk classes, thorough manual testing, and preserving fallback globals during transition.

**This is the holistic "6-class refactor" shape** for the user's S5421 view.

---

**Read-Only Analysis provided by Researcher Agent.**

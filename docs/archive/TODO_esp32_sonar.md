# TODO — esp32 sonar S5421 + S1820 (GLM-prescribed, StepFun-implements)

Pure mechanical renames + one global→accessor. NO behaviour change. Public API unchanged.

## ISSUE 1 — cpp:S5421 (mutable global), can-bridge.ino:479
`static SerialEventLogger serialEventLogger;` — can't be const (non-const virtuals + raw-pointer storage in FirmwareApp); can't be stack-local (must outlive setup()).
FIX — function-local static accessor (the EXISTING pattern at can-bridge.ino:270-273 `ledOutput()`/`statusLed()`):
1. DELETE the global at can-bridge.ino:479.
2. ADD accessor right after the SerialEventLogger struct's closing `};` (~.ino:478):
   `inline SerialEventLogger& serialEventLogger() { static SerialEventLogger inst; return inst; }`
3. UPDATE the single call site can-bridge.ino:519: `firmwareApp.setEventLogger(serialEventLogger);` → `firmwareApp.setEventLogger(serialEventLogger());` (parens).

## ISSUE 2 — cpp:S1820 (FirmwareApp 23 fields > 20), FirmwareApp.h
PARAM-OBJECT (NOT SRP-split — team decided against splitting FirmwareApp, tasks #101/#102). Bundle 2 cohesive clusters → net 23→19 fields.

Bundle A — DiscoveryState (4 fields → 1). Add POD struct near FirmwareCallbacks (after FirmwareApp.h ~L43):
```cpp
struct DiscoveryState {
    bool started = false;          // was discoveryStarted_
    bool enabled = true;           // was discoveryEnabled_
    bool clientConnected = false;  // was clientConnected_
    uint32_t lastBroadcastEventIntervalMs = 0;
};
```
Replace the 4 fields (discoveryStarted_, discoveryEnabled_, clientConnected_, lastBroadcastEventIntervalMs_) with `DiscoveryState discovery_;`. In FirmwareApp.cpp rename: discoveryStarted_→discovery_.started, discoveryEnabled_→discovery_.enabled, clientConnected_→discovery_.clientConnected, lastBroadcastEventIntervalMs_→discovery_.lastBroadcastEventIntervalMs. DELETE `discoveryStarted_(false)` from the ctor init list (struct in-class init handles it).

Bundle B — ObservabilityState (4 fields → 1). Add POD struct next to DiscoveryState:
```cpp
struct ObservabilityState {
    IEventLogger* logger = nullptr;  // was eventLogger_
    std::string clientIp;            // was clientIp_
    int lastLedPattern = 0;
    int lastDisconnectReason = 0;
};
```
Replace the 4 fields (eventLogger_, clientIp_, lastLedPattern_, lastDisconnectReason_) with `ObservabilityState observability_;`. In FirmwareApp.cpp rename: eventLogger_→observability_.logger, clientIp_→observability_.clientIp, lastLedPattern_→observability_.lastLedPattern, lastDisconnectReason_→observability_.lastDisconnectReason. Update inline getters in FirmwareApp.h: getClientIp()→`return observability_.clientIp;`, getCurrentLedPattern()→`return observability_.lastLedPattern;`.

GREP firmware/tests/ + firmware/mocks/ for the OLD private field names + update any direct access (check FirmwareApp_test_fixture.h). Public API unchanged → existing FirmwareApp tests pass unchanged.

## GATE (before commit)
`make firmware-host-tests` (307 unchanged) + `make firmware` (xtensa) + `make sonar-scan-esp32` (expect 0 OPEN) + full `make gate` for the commit. Build GREEN + esp32 sonar ZERO.

## HARD RULES
No NOSONAR, no skipped tests, no push, make targets only (NOT rm -rf). DO NOT TOUCH: behaviour, public API, pattern table, sonar/build config, FirmwareCallbacks, owned-manager fields, DI/config fields, previousWifiState_, initialized_, ntpStarted_, serialQuietUntilMs_, callbacks_. Bundles A & B + the accessor only.

ONE commit on sonar_fixes: `cpp:S5421 cpp:S1820: SerialEventLogger accessor + bundle FirmwareApp discovery/observability state`. YOU MUST commit (git add + git commit) + report the hash. Then STOP. Do NOT push.

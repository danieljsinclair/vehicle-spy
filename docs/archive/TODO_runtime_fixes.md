# Runtime Fixes — Deterministic Mock-Seam TDD Prescription (GLM-architected)

> Root cause of all issues: existing mocks simplified away real behaviour
> (instant cancel, instant drop). Fix: deterministic state machines + fake
> clock. NO threads, NO sleeps, NO real-time tests.

## Shared assets (build FIRST — reused by #1 + #2)

### iOS FakeClock + FakeVMClock (new files in VehicleSimTests/):
```swift
final class FakeClock { private(set) var now: Int = 0; func advance(_ ms:Int){ now+=ms } }
protocol VMClock { func nowMs() -> UInt64 }
struct WallVMClock: VMClock { func nowMs()->UInt64{ UInt64(Date().timeIntervalSince1970*1000) } }
final class FakeVMClock: VMClock { private(set) var now: UInt64=0; func nowMs()->UInt64{now}; func advance(_ ms:UInt64){now+=ms} }
```

### iOS DiscoveryListening protocol (new file VehicleSim/DiscoveryListening.swift):
```swift
enum DiscoveryListenerState { case setup, ready, cancelling, cancelled, failed }
protocol DiscoveryListening: AnyObject {
    func startListening(onState: @escaping (DiscoveryListenerState) -> Void,
                        onPacket: @escaping (Data, String) -> Void) throws
    func cancelListening()
    var state: DiscoveryListenerState { get }
}
```
ESP32DiscoveryListener becomes a thin coordinator (owns injected DiscoveryListening). NWPathDiscoveryListener wraps the current NWListener (behavior-preserving).

### iOS ConnectionLivenessProbe (new file VehicleSim/ConnectionLivenessProbe.swift):
```swift
protocol ConnectionLivenessProbe: AnyObject {
    func recordHeartbeat(nowMs: UInt64)
    func isConnectionStale(nowMs: UInt64) -> Bool
}
```

## Execution order (one commit per step, gate-green each)

### Commit 1: Shared seams (behavior-preserving refactor)
FakeClock + FakeVMClock + DiscoveryListening protocol + NWPathDiscoveryListener wrapper + ConnectionLivenessProbe. No behavior change.

### Commit 2: #1 NWListener async cancel (NWError 48)
- FakeDiscoveryListener: cancelListening() → .cancelling; tick(clock) → .cancelled.
- Fix: stop() waits for .cancelled before allowing rebind.
- Blind tests: cancelTransitionsToCancellingImmediately, noCancelledBeforeTick, cancelledAfterTick, stopBlocksUntilCancelled, rapidStopStartNoPortConflict.

### Commit 3: #2 VMClock + ConnectionLivenessProbe injection
Inject VMClock (default WallVMClock) + ConnectionLivenessProbe (default HeartbeatLiveness(timeoutMs:1000)) into VehicleViewModel. Retrofit the 11 ReconnectTests + ModeFlip suite to use FakeVMClock (de-flake).

### Commit 4: #2 Heartbeat liveness (silent drop)
- recordHeartbeat on fresh data; isConnectionStale(1s) → disconnect + reconnect.
- Blind tests: heartbeatReceivedResetsStale, heartbeatMissedForOneSecondIsStale, silentESPTriggersReconnect, activeDataPreventsFalseDisconnect, reconnectBackoffAdvancesWithFakeClock.

### Commit 5: #3 Client wiring investigation
Extract IClientConnectionSource protocol → host-testable → prove the divergence (global WiFiClient vs TcpServerManager adopted client). Fix based on finding.

### Commit 6: #4 WiFi retry + AP serial event
USER POLICY (strict): AP mode ONLY for **definitive auth failure** (wrong password — the
credentials are definitively rejected; it can NEVER work). EVERYTHING else → STA retry
**forever** (router reboot, AUTH_EXPIRE, SSID temporarily gone — all transient, all might
recover). Both sides (iOS + ESP32) are constantly trying to find each other.
- The auth→AP gate should include ONLY definitive-auth reason codes: 4WAY_HANDSHAKE_TIMEOUT(15),
  802_1X_AUTH_FAILED(21), AUTH_FAIL(202). NOT AUTH_EXPIRE(2) or AUTH_LEAVE(3) (transient).
- Reuse the EXISTING firmware FakeClock (firmware/tests/FirmwareApp_characterization_test.cpp:39).
- Emit [EVENT] wifi_ap_fallback reason=<n> via FirmwareApp (new ctx.escalatedToApReason field).
- Blind firmware tests: wrongPasswordGoesToAp (reason 15), authFailGoesToAp (reason 202),
  authExpireRetriesStaForever (reason 2 → stays WIFI_CONNECTING), routerRebootRetriesSta (reason 4 → retry),
  apFallbackEmitsSerialEvent.

### Commit 7: #5 WiFi resilient reconnect — IP-aware tcpRestart + user-friendly serial (GLM-prescribed)
USER GOAL: less-aggressive reconnect. Don't tear down the TCP server on every WiFi blip —
only restart it when the IP actually CHANGED. Prioritize (a) quickest recovery + (b) maintain
connectivity (survive a brief disconnect, don't drop connections unnecessarily).

ROOT CAUSE (verified against live source):
- `tcpRestart=true` fires on EVERY transition, IP-aware or not. Two sites in WiFiManager.cpp:
  1. `ConnectingStateHandler` status==3 (WL_CONNECTED) L73 — first-connect success.
  2. `ConnectedStaStateHandler::execute` L155 — drop path (WIFI_CONNECTED → WIFI_CONNECTING,
     `tcpRestart=true`). This is the reconnect: drop then re-CONNECTED.
- The flag is consumed in can-bridge.ino::restartTcpServerIfNeeded() L430-443: on
  `shouldRestartTcpServer()` it unconditionally does `tcpServer().end()` + `.begin()` + the
  user-facing message `Serial.printf("...Restarting TCP server on IP change...")` (L432).
- WiFiManager does NOT track the last-known STA IP anywhere — `localIP()` (IWiFi L71) is read
  only for the `[EVENT] wifi_connected ip=...` emit (FirmwareApp.cpp:176). There is no field to
  compare against. So the IP-aware gate the user wants requires a NEW Context field.
- The user-facing string "Restarting TCP server on IP change" is misleading: the IP usually did
  NOT change (same DHCP lease after a brief blip). Exposes an internal implementation detail.

PRESCRIPTION:

A. TRACK THE LAST-KNOWN STA IP. Add to `WiFiState::Context` (WiFiManager.h:23):
```cpp
std::string lastConnectedIp;   // STA IP captured at the last WIFI_CONNECTED entry (empty before first connect)
```

B. CAPTURE IP ON CONNECTED ENTRY. In `applyStateTransition` (WiFiManager.cpp:328+), when the
   transition enters `WIFI_CONNECTED` (i.e. `transition.nextState == WIFI_CONNECTED &&
   ctx.state != WIFI_CONNECTED`), set `ctx.lastConnectedIp = wifi_.localIP();`. This is the
   single authoritative capture point for both first-connect and reconnect (DRY). NOTE:
   applyStateTransition is on WiFiManager (has `wifi_`), so no new seam needed.

C. IP-AWARE tcpRestart ON RECONNECT. Gate the RECONNECT tcpRestart by an IP comparison:
   add a pure helper (namespace-level, host-testable — matches the existing
   shouldRetryWiFi/shouldFallbackToApMode pattern):
```cpp
// True if the TCP server must be restarted after this reconnection.
// Restart only when the new STA IP differs from the last-known IP (e.g. DHCP gave a new lease).
// Same IP after a brief blip → the listening socket may have survived → keep it (faster recovery,
// maintains connectivity). First-ever connect has no lastConnectedIp → must restart (bind the socket).
bool shouldRestartTcpServerForReconnect(const std::string& newIp, const std::string& lastConnectedIp);
// → return lastConnectedIp.empty() || newIp != lastConnectedIp;
```
   Wire it into `ConnectedStaStateHandler::execute` (the drop→CONNECTING path): the handler
   currently returns `StateTransition(WIFI_CONNECTING, true, false)` unconditionally. The
   tcpRestart decision can't be made at drop time (we don't yet know the new IP). Instead:
   - At DROP (ConnectedStaStateHandler): keep `tcpRestart=false` for now; record that a reconnect
     is pending (new `Context::bool reconnectPending = true`).
   - At RE-CONNECTED (ConnectingStateHandler status==3, L72-73): call
     `shouldRestartTcpServerForReconnect(wifi_.localIP(), ctx.lastConnectedIp)` and set the
     transition's `tcpRestart` to THAT result (currently hard true). Clear `reconnectPending`.
   - First-ever connect: `lastConnectedIp` is empty → helper returns true → restart (binds the
     socket for the first time). Behavior-preserving for the cold-start case.
   This keeps all IP knowledge inside WiFiManager (FirmwareApp/.ino unchanged structurally).

D. USER-FRIENDLY SERIAL (can-bridge.ino:430-443). The message must not expose the restart
   implementation detail and must not say "IP change" when there wasn't one. Two changes:
   1. When `shouldRestartTcpServer()` is true, the .ino still does end()/begin() (the restart is
      real), but relabel the message to a neutral, user-friendly line — e.g. drop the bespoke
      printf entirely and rely on the existing `[STATE] wifi=WIFI_CONNECTED` + `[EVENT]
      wifi_connected ip=...` lines (FirmwareApp already emits both on connect). I.e. **omit the
      message** (the user already sees wifi reconnect via [STATE]/[EVENT]).
   2. If a line is still wanted for diagnostics, relabel to something that does NOT claim an IP
      change, e.g. `Serial.printf("%s[INFO] WiFi reconnected%s\r\n", YELLOW, NC);` — generic, no
      internal-model leak. Keep it minimal; the diagnostic detail (old vs new IP) belongs at
      DEBUG verbosity, not the default user-facing stream.
   Recommend option 1 (omit) per the user's "don't expose the TCP-server-restart implementation
   detail" directive — the [STATE]/[EVENT] stream already conveys the reconnect.

E. HOST-TEST SEAM EXTENSION (REQUIRED — currently a blocker). `WiFiMock::localIP()`
   (firmware/mocks/WiFiMock.h:62) returns a HARDCODED "192.168.1.100" — it is NOT controllable
   per-test. The "reconnect with a DIFFERENT IP" blind test cannot be written without this.
   Extend WiFiMock: add a `std::string injectedLocalIp_` member (default "192.168.1.100"), a
   `void setLocalIP(const std::string& ip)` setter, and have `localIP()` return it when STA+connected.
   This is a behavior-preserving mock change (commit-7-prereq, test-only file).

BLIND HOST TESTS (append to firmware/tests/WiFiManager_test.cpp, reuse existing FakeClock +
WiFiMock + WiFiManagerTest fixture). All deterministic, zero real time:
1. testFirstConnectRestartsTcpServer — cold start, lastConnectedIp empty → re-CONNECTED fires
   tcpRestart=true (binds socket). (Characterizes the preserved cold-start behavior.)
2. testReconnectSameIpKeepsTcpServer — set lastConnectedIp="192.168.1.100"; drop; reconnect with
   localIP still "192.168.1.100" → tcpRestart does NOT fire (socket survives, fastest recovery).
3. testReconnectDifferentIpRestartsTcpServer — set lastConnectedIp="192.168.1.100"; drop;
   reconnect with localIP="192.168.1.105" → tcpRestart FIRES (new lease, must rebind).
4. testReconnectAfterLongDropRestartsTcpServer (SAFETY) — set lastConnectedIp; drop; reconnect
   with the SAME ip but after a long disconnect window (FakeClock advance past a threshold, e.g.
   > N ms). Even with the same IP, restart as a safety net (socket likely stale after a long
   outage). → Add a `Context::uint32_t disconnectStartMs` + a `shouldRestartTcpAfterLongOutage`
   duration check in the helper, OR fold into the helper as a third arg
   `(newIp, lastConnectedIp, outageMs)`. Kilo to pick: extend the helper signature vs a separate
   guard. RECOMMEND the 3-arg helper (one pure function, all restart-reasons visible).
   `return lastConnectedIp.empty() || newIp != lastConnectedIp || outageMs > LONG_OUTAGE_MS;`
   (LONG_OUTAGE_MS e.g. 30000 — tunable, keep as a named constexpr, NOT a pattern-table value).
5. testShouldRestartTcpServerForReconnect_PureHelper — direct unit test of the pure helper across
   the 2x2 matrix (empty/filled lastIp × same/different newIp) + the long-outage branch. Fast,
   no fixture.
6. testUserFacingSerialDoesNotLeakRestartDetail — (host-testable via the existing IEventLogger
   seam in FirmwareApp_characterization_test): on a same-IP reconnect, assert NO log line
   matching "Restarting TCP server" / "IP change" is emitted (message omitted or relabeled). On
   a different-IP reconnect, assert the neutral relabeled line (if option 2 is chosen) OR assert
   only [STATE]/[EVENT] lines appear (option 1).

GATE (before commit): `make firmware-host-tests` (305+6 new green) + `make firmware` (xtensa
produces can-bridge.ino.bin with the relabeled/omitted message) + `make sonar-scan-esp32` (0
OPEN — verify the new helper + Context fields don't trip S1820; they shouldn't: Context is a POD
struct not a class, and the new helper is namespace-level like the existing four). Behavior-
preserving parts (E mock extension, B capture-only) MAY be a separate prep commit if clean;
the IP-aware tcpRestart (C) + serial relabel (D) go in the behavior-changing commit.

SCOPE GUARD: this is a WiFiManager + can-bridge.ino message change ONLY. Do NOT touch the iOS
side (commit 4 heartbeat already handles silent-drop reconnect), pattern-table values, or
sonar/build config. The `tcpRestart` flag on the AP-mode paths (L251 clears, L261/342 set) stays
as-is — AP mode is a different network interface, always restart there.

## Hard rules
NO NOSONAR, NO skipped tests, NO push, make targets only (no rm -rf). Behavior-preserving refactors (commit 1, 3, 5, 7-prep) MUST be separate from behavior-changing fixes (commit 2, 4, 6, 7). iOS: make ios-test-gate (grep TEST SUCCEEDED) + ios-analyze. Firmware: make firmware-host-tests + make sonar-scan-esp32 (0 OPEN). Do NOT touch pattern-table values or sonar/build config.

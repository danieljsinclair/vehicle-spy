# FINAL Coverage Improvement Report — vehicle-sim (2026-07-21)

## EXECUTIVE SUMMARY

**vehicle-spy:** 75.8% → Target >80% (15.8% gap)
**Analysis basis:** TODO_vspy_coverage_tests.md Phase-1 live assessment

---

## PRIORITY 1: SimulationSignalSource.cpp (24 lines, 0% covered)

CRITICAL FINDING: The adapter is INCOMPLETE. `latestSignal()` returns the default-constructed local member instead of polling `simulator_->getLatestSignal()`. This is a functional gap, not just missing tests.

### Behavior Contracts to Assert
- **start()** → simulator.initialize() + simulator.start() called; worker loop updates latestSignal_
- **stop()** → simulator.stop() called; worker thread joins
- **latestSignal()** → returns vehicle signal when running, timestamp=0 when not
- **Double-start/idempotency** → second start is no-op

### Architect-2 Critique (agreed)
- This tests REAL production code (VehicleSimulator adapter), not mock-testing
- The signal-update missing is worth testing - it is business logic (live simulator values)
- No fragile assertions needed; use method-call verification on mock

### Exception Risk → Debug Assert
None. Defensive guards appropriate.

### ACTION
Complete the polling loop OR DELETE if no production callers (per liveness gate).

---

## PRIORITY 2: SecureTcpTransport.cpp (283 lines, 79.9% / 57 uncovered)

### Already Covered
- Handshake success/failure with correct/wrong key
- Connection refused
- Clean disconnect returns nullopt
- Stop-flag interrupts nextLine()
- Frame reassembly (split frame across recv calls)
- Tampered ciphertext detection
- Raw buffer overflow protection

### Uncovered Behaviors (57 lines)
- Reconnect loop edge cases (EINTR handling)
- Frame length extraction corner cases
- recvTimeoutUs_ clamping (100ms floor, negative fallback)
- stop-requested during poll termination

### Action Required
ISocket injection refactor (mirrors TCPTransport Phase-1) to test edge cases without real loopback.

---

## PRIORITY 3: DemoSignalSource (existing tests cover computeNextSignal well)

### Missing Coverage
Thread lifecycle in generateSignals(): the `sleep_for(intervalMs_)` branch when `running_` becomes false during sleep - stop-racing scenario.

### Action
Add explicit double-stop/idempotency test.

---

## PRIORITY 4: BLESignalSource (existing tests cover parsing + lifecycle)

### Missing Coverage
Double-stop idempotency: `if (!connected_) return;` guard in stop().

### Action
Add explicit double-stop test.

---

## Files to SKIP

| File | Reason |
|------|--------|
| main.cpp, cli/*.cpp | Composition roots (low ROI) |
| USBTransport.cpp | libusb platform I/O |
| UDPDiscovery.cpp | No ISocket seam |
| BLEManager.cpp | Thin forwards, platform layer tested |
| StatusLED.cpp | Arduino veneer (deferred) |

---

## Recommendations

1. **SimulationSignalSource first** — either complete it OR delete if no callers (per liveness gate)
2. **SecureTcpTransport second** — ISocket injection for edge-case coverage
3. **Demo/BLE signal sources** — add explicit idempotency tests

---

## No Debug-Assert Recommendations

All uncovered paths handle external boundaries appropriately. No internal invariants violated.
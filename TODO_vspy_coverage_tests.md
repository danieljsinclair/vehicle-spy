# TODO_vspy_coverage_tests.md — real coverage for testable Category B .cpp (vehicle-spy)

> Read `TODO.md` first (constraints, gate, delegation model, self-check). **Goal: write REAL unit tests for the testable-but-uncovered `.cpp` in vehicle-spy (Category B, ~242 ROI lines).** This is test-writing + small SRP refactors to enable testing — **NOT** more `.ino`→vanilla extraction (that is essentially complete). Reuse the existing seams (`FakeSocket`, `FakeClock`, `MockBLEManagerBase`, `IDiscoveryListener`). The Coverage Gap Analysis (pasted into the team brief) is the target list.

## SCOPE — Category B, value order (per the gap analysis)
1. **`src/domain/DemoSignalSource.cpp`** (79 lines, ~70% uncovered). Extract the per-step signal math out of the `generateSignals()` thread loop into a **pure** function `computeNextSignal(phase) → VehicleSignal` (SRP: separate pure math from threading/timing). Test the math with `FakeClock`. Blind-TDD the math behaviour first, then extract (red→green→refactor).
2. **`src/ble/BLEManager.cpp`** (113 lines, ~48% uncovered — gaps in the state machine). Happy-path tests: `setDataReceivedCallback` + data dispatch, connect/disconnect state transitions. **Reuse `MockBLEManagerBase`** (exists). Mock the BLE provider/callbacks; assert the state machine, not the platform.
3. **`src/pipeline/SecureTcpTransport.cpp`** (283 lines, ~57 uncovered). TLS handshake happy path + certificate-validation failure. **Reuse the TCPTransport `FakeSocket` seam** — script the handshake bytes; assert handshake success + cert-fail behaviour. No real TLS/network.
4. **`src/domain/BLESignalSource.cpp`** (35 lines, 100%). Thin adapter — fully testable; mock the BLE source it wraps.
5. **`src/config/ConfigLoader.cpp`** (21 lines, 100%). Trivial happy-path test (it's I/O-free right now — test it **before** I/O gets bolted on; cheapest window).
6. **`src/domain/SimulationSignalSource.cpp`** (24 lines, 100% — **incomplete refactor**). Do NOT delete, do NOT ignore. **Complete the refactor: wire it into the signal-source DI pipeline + test it** (per the project rule `incomplete-refactor-code-not-dead`).

## OUT OF SCOPE (Category A — do not spend effort here)
Composition roots / raw sockets, intentionally not unit-tested, or need a seam first:
- `src/main.cpp`, `src/cli/BLERunContext.cpp`, `src/cli/BLEConnectionManager.cpp`, `src/cli/LiveRunContext.cpp` (composition roots — wire deps; low per-line ROI; defer).
- `src/pipeline/USBTransport.cpp` (libusb-bound — platform).
- `src/discovery/UDPDiscovery.cpp` (raw socket, no `ISocket` seam — **flag, don't fix**: testing it needs an `ISocket` injection like TCPTransport got; that's a separate, higher-effort refactor. Note it; do not attempt in this workstream unless the lead adds it.)

## QUALITY BAR (mandatory — same as PosixSocket)
- **Test architect authors** the tests (from the observable behaviour spec, blind where non-trivial); a **second architect critiques HARSHLY** for value — cut anything **fragile** (hard-coded timing/state/order), **mock-testing** (asserts the mock, not the real code), **tautological** (asserts the test's own setup), or **low-value** (duplicates a simpler test). Each surviving test must state which production path it exercises + why it matters.
- **TDD:** red→green→refactor. RED-phase tests **must compile**. Don't weaken an assertion to force green — if a path can't be tested cleanly, STOP + flag.
- **SOLID / SRP / OCP / DI** in both the tests and any enabling refactor (e.g. the `computeNextSignal` extraction is SRP-motivated, not a coverage hack).
- **Fast + hermetic:** each test <100ms (ideally <10ms), **no real I/O** (no real sockets/files/network/sleeps) — mock via the seams. (PosixSocket's real-loopback tests are the ONE exception; do not add more real-I/O tests here.)

## ACCEPTANCE
1. Category B files above gain real coverage; report per-file % before → after (live Sonar measures).
2. `SimulationSignalSource` refactor **completed** (wired + tested), not deleted.
3. Every new test passes the harsh-critic value gate; per-test runtime <100ms; zero real I/O.
4. vehicle-spy coverage % rises on the **executable-line** denominator (after the instrumentation fix lands — coordinate with Kilo's `TODO_vspy_coverage_instrumentation.md` so the denominator is honest).
5. `make gate` green; live Sonar vehicle-spy OPEN=0 (new tests must not introduce issues — this is test-only, expect Sonar-neutral). No push. No suppression. No skipped tests.

## SELF-CHECK + REPORTING (per TODO.md — corrected protocol)
Spawn a verifier teammate that independently: re-runs the suite (per-test runtime, zero real I/O), re-checks **live** Sonar vehicle-spy OPEN delta (flag ANY new issue, not just the brief items), confirms the critic's rejections were applied, and spot-checks that new tests exercise REAL production code (grep: the SUT class is the real one, not a mock). Report per-file coverage before → after + the critic's verdict.

## DO NOT
- Don't push. Don't suppress. Don't re-extract `.ino`→vanilla (it's done). Don't touch Category A. Don't add real-I/O tests. Don't delete `SimulationSignalSource` — finish it. Don't exclude files to game coverage (if something shows 0%, the answer is a test or the instrumentation fix, never an exclusion).

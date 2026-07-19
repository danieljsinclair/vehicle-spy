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

---

## PHASE-1 LIVE ASSESSMENT (2026-07-19) — read-only assessor; supersedes stale premises above

Live baseline (`make coverage-clean` + `make coverage-run`, exit 0; Sonar API cross-check, **zero drift**):
- **vehicle-spy = 74.2%** (6019 to_cover / 4469 covered / 1550 uncovered).
- **Category B = 246 uncovered = 15.9%** of all uncovered. Honest ceiling this phase ≈ vehicle-spy **76–77%** (per-file wins dramatic; headline modest — value-coverage, not a percent grab).

Per-file (live): DemoSignalSource 30.4% / 55 uncov (extract pure `computeNextSignal`); BLEManager 52.2% / 54 (low-value forwards — defer); SecureTcpTransport 79.9% / 57 (inject `ISocket`); BLESignalSource 0% / 35 (existing seam; +OCP `BLEManagerBase*`); ConfigLoader 0% / 21 (pure, I/O-free today); SimulationSignalSource 0% / 24 (extract `IVehicleSimulator` + wire DI — mandatory complete).

**PREMISE CORRECTIONS (assessor found the original SCOPE framing partly stale):**
1. **SecureTcpTransport does NOT use the FakeSocket/ISocket seam** (that belongs to `TCPTransport`, a different class). Its two originally-named targets (handshake happy-path + cert-validation fail) are **ALREADY covered** by the existing real-loopback test. The remaining 57 branches need an `ISocket`-injection enabling refactor (same shape as TCPTransport Phase-1) — a veneer-enabling extraction, not a coverage task. **DECISION (lead, 2026-07-19): ISocket injection IS in scope this phase** (capstone, after the 4 smaller files).
2. **BLEManager "state-machine / connect-disconnect gaps" framing is STALE** — connect/disconnect ARE covered; the 54 uncovered are thin 2-line facade forwards whose ELM327/OBD2 logic is already tested at the platform layer (tests bypass the facade). DEFER exhaustive; at most a null-platform safety test (optional).

**AGREED PHASE-2 SCOPE (lead-signed 2026-07-19; ConfigLoader UPDATED 2026-07-19)** — sequential, one gate-green `test:`-prefixed commit per area, author→harsh-critic→independent-verifier per file:

**ConfigLoader — DROPPED + DELETED (not covered).** Lead independent grep proved it has ZERO production callers; research confirmed it's a 2026-04-05 repo-init placeholder stub, superseded by the shipped `vehicle_sim::domain::VehicleConfig` + `VehicleConfigRegistry` + `DefaultVehicleConfigs` (2026-04-30). Complete-but-dead, NOT an unfinished refactor. Deleted (3 source files: include/vehicle-sim/config/ConfigLoader.h + src/config/ConfigLoader.cpp + src/config/ConfigLoader.h dup; 2 CMakeLists entries; test/config/ConfigLoader.test.cpp), gated by `make gate` incl. ios-analyze green. Lesson: the assessor measured coverage % but NOT liveness — dead ConfigLoader slipped through. **LIVENESS GATE now hard:** every Files 2–5 author + critic must confirm the SUT has ≥1 production caller; zero callers ⇒ STOP + flag, no vanity tests.

REVISED FILE ORDER (4 files):
1. **DemoSignalSource** — extract pure `computeNextSignal(phase)`; test signal math + gear-select. (was File 2)
2. **BLESignalSource** — test real `onDataReceived` parsing via `BLEManager`+`setPlatform(MockBLEManagerBase)`; +OCP depend on `BLEManagerBase*`.
3. **SimulationSignalSource** — COMPLETE refactor: extract `IVehicleSimulator` + wire `PipelineFactory` DI + test. (mandatory; highest veneer value)
4. **SecureTcpTransport** — `ISocket`-injection refactor (mirror TCPTransport Phase-1) + test reachable branches. (capstone; biggest scope)
**DEFERRED:** BLEManager (low-value forwards); UDPDiscovery (needs its own seam — separate workstream).

**Through-line:** vehicle-spy coverage as **FOUNDATION-LAYING for the esp32/iOS thin-veneer sonar-zero path** — solidify the vanilla core + its abstractions (`IVehicleSimulator`, `ISocket`, `BLEManagerBase`) so platform thinning lands on a tested, interface-driven base.

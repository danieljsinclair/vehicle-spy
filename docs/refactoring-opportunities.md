# Refactoring Opportunities

This document tracks potential future refactoring opportunities identified during code review and development. These are optional improvements, not bugs.

## 1. TelemetryRunner Production Busy-Wait Spin

**Location:** `src/cli/TelemetryRunner.cpp:107`

**Current Behavior:**
The `run()` loop uses an unconditional `std::this_thread::sleep_for(std::chrono::milliseconds(SPIN_SLEEP_MS))` (10ms) on each iteration, resulting in a busy-wait spin that polls the atomic `g_running` flag.

**Proposed Refactor:**
Replace the spin with a `std::condition_variable` blocking wait that is notified by `requestStop()`, eliminating the polling overhead.

**Key Constraint:**
`signalHandler` is a C signal handler (registered via `std::signal`) and CANNOT safely touch mutexes or condition variables (non-async-signal-safe). The handler must remain an atomic-flag setter only, with the loop re-checking the flag. This constraint makes a clean condition_variable design non-trivial — the loop cannot simply `cv.wait()` because the signal handler cannot safely `cv.notify_one()`.

**Estimated Benefit:**
- Eliminate 10ms polling latency during shutdown
- Reduce CPU usage in production telemetry runs

**Effort:** Medium  
**Risk:** Medium (signal handler safety is subtle; incorrect use can cause undefined behavior)

---

## 2. DBC Re-Parse Overhead in Tests

**Locations:**
- `test/integration/DBCPipelineIntegration.test.cpp:12`
- `test/integration/AudiOBD2LiveDecoding.test.cpp:11`

**Current Behavior:**
The `DBCFileParser` is a per-test fixture member, causing large DBC files like `vw_mlb.dbc` (~224KB) to be re-parsed on every test in the suite. This adds approximately 120ms per test suite.

**Proposed Refactor:**
Hoist the parsed DBC result to `SetUpTestSuite()` (for googletest) or a static lazy-initialization pattern so the file is parsed once per test suite and shared across tests. The parser is stateless, so sharing the parsed result across tests is safe.

**Estimated Benefit:**
- ~120ms saving per test suite run
- Faster test feedback during development

**Effort:** Medium  
**Risk:** Low (parser is stateless; sharing is safe)

---

## 3. EventDispatcher Race Test Timing

**Location:** `test/domain/EventDispatcher.test.cpp:187-204`

**Current Behavior:**
The `ConcurrentRegistrationDuringDispatch` test uses `std::this_thread::sleep_for(std::chrono::milliseconds(1))` calls to deliberately force register-vs-dispatch race interleaving. The sleeps are intentional — they create the race window by timing.

**Proposed Refactor (Optional):**
Replace the timing-based sleeps with an `std::atomic` barrier or countdown latch for more deterministic race forcing. However, this is OPTIONAL: the current sleeps may be preferable because they better surface real race conditions that timing-dependent tests might miss.

**Key Trade-off:**
- **Current approach (sleeps):** Less deterministic, but better at catching real races that occur due to timing variations.
- **Atomic barrier approach:** More deterministic, but may reduce coverage by making the timing too predictable and potentially missing real race scenarios.

**Estimated Benefit:**
- More deterministic test timing (if atomic barriers used)
- Potentially faster test execution (no 1ms waits)

**Effort:** Small-Medium  
**Risk:** Medium (could weaken race coverage by making the test too deterministic)

---

## Notes

- All refactors should follow SOLID principles, especially SRP and OpenClosed
- Use TDD methodology: red phase tests must compile
- Prefer async/await patterns where applicable (C++ equivalents: futures, coroutines)
- Consider SOLID, especially DI for testability
- Avoid violating OpenClosed with conditionals — use Types, Factories, and Strategy pattern

## Convention

Comments in code use the `NOTE:` marker to avoid SonarQube `TODO:` flagging. This convention is used consistently across the codebase for tracking improvement opportunities without triggering static analysis warnings.

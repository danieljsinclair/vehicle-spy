# TODO — LED single-driver consolidation + deterministic serial output tests

Owner: kilo worker `led-serial-fix` (author). Critic: GLM PA `fwapp-refactor` (separate pass).
Branch: work on `sonar_fixes` (current). HEAD `5808965`, clean tree.
**FIRST ACTION: print exactly `ACK led-serial-fix alive` (proof of life), then begin.**

> **BRIEF CORRECTION (critic early-warning, VERIFIED by lead 2026-07-27):**
> Deliverable A's original premise ("[STATE] cannot be unit-tested; add a serial
> seam") was WRONG. `[STATE]` is ALREADY fully unit-tested. See §A below. Do NOT
> add `ISerialOutput`. Focus your effort on §B (the genuine work).

## Background (why)

The ESP32 can-bridge firmware has **two parallel per-loop LED drivers with no
arbitration**: `WiFiManager::update()` AND `TcpServerManager::cycle()` both call
`StatusLED::setPattern()` on a shared `StatusLED`. Last-writer-wins → at runtime
the LED shows the wrong pattern (observed: OFF-800/ON-200, the *inverse* of
`WIFI_CONNECTED`'s ON-800/OFF-200; and LED goes off when a client connects).

Separately, the user wants the unit-tested state machine to **produce the right
text over serial**, asserted in unit tests. **For `[STATE]`, this is ALREADY
satisfied** via `LoopHeartbeat::snapshot()` (see §A). The genuine untested serial
output is `[LED]` diagnostics (direct `Serial.printf`, host-compiled-out). And
the user's live-device symptom ("`[STATE]` always CONNECTED") is a **runtime/
wiring issue, NOT a unit gap** — the unit proves the string is correct per state.

## Goal (TDD red-green-refactor)

### A. Serial output — [STATE] ALREADY TESTED; optional [LED] seam only

**`[STATE]` is ALREADY fully unit-tested** via `LoopHeartbeat::snapshot()`:
`tick()` builds the `[STATE]` text into a host-testable `std::string` (NO `Serial`
call in vanilla; the actual `Serial.print` is in `can-bridge.ino:598`).
`firmware/tests/LoopHeartbeat_test.cpp` already asserts:
- `FiresAfterInterval`: exact literal `"[STATE] uptime=5000ms wifi=DISCONNECTED monitor=idle\r\n"`
- `MapsAllWiFiStates`: all 5 states (DISCONNECTED/CONNECTING/CONNECTED_STA/CONNECTED_AP/RECONNECTING) + unknown→`UNKNOWN`
- `monitor=ACTIVE` and `monitor=idle`

**DO NOT add an `ISerialOutput` seam for `[STATE]` — it is REDUNDANT** (net-new
production surface for ZERO new coverage; violates "don't make coverage worse").
Just **CONFIRM `LoopHeartbeat_test.cpp` is green** (part of the 307) and move on.

**Optional, only if it adds real value** — the `[LED]` diagnostic strings
(`StatusLED.cpp:224,285`) ARE direct `Serial.printf` behind `#ifdef ARDUINO`
(compiled out on host), so they genuinely lack a host-test seam. IF you choose to
assert `[LED]` output in host tests, add the seam **there**, **reusing the
existing `IFactoryResetLogger` pattern** (`FactoryResetCheck.h:30` — "replaces
Serial.printf in the .ino"). **Do NOT invent `ISerialOutput`.** Skip this entirely
if it does not strengthen the consolidation in §B.

**The "[STATE] always CONNECTED" device symptom is NOT yours to unit-test-fix** —
it is a runtime/wiring issue (is `getWiFiState()` returning the right value at the
`.ino` call site? is `tick()` called with the live state? is the state machine
actually transitioning?). Flag your findings for the lead's device-smoke task; do
NOT add a unit test to "fix" a runtime symptom.

### B. Consolidate to ONE SOLID LED driver (kill the race) — THE GENUINE WORK

- **Evaluate both** current pattern-selection logics (`WiFiManager::update` /
  `applyStateTransition`'s wifi-state→pattern vs `TcpServerManager::cycle`'s
  client→pattern) against **spec-compliance + SOLID (SRP/OCP/DI)**. Pick the
  better as the base; merge the other's correct behaviors in. Produce a
  **single driver** that owns LED pattern as a pure function of the **combined
  system state**: `(wifiState, clientConnected) -> Pattern`.
- Remove the duplicate `setPattern()` call site from the loser so there is
  exactly **one owner** of the LED per loop. This kills the race
  *architecturally* — NOT a sticky/arbitration patch.
- **RED first**: a test asserting pattern selection is the pure function above
  — e.g. while `clientConnected`, the pattern is `CLIENT_CONNECTED` **regardless**
  of a concurrent wifi-searching state (the exact race symptom). Assert the
  **`Pattern` ENUM**, never ms durations.
- **GREEN**: consolidate.

## HARD RULES (non-negotiable)

- **TDD**: red tests FIRST (red tests MUST compile — assert correct behaviour,
  not failure). Then green. **Line coverage must NOT get worse** — measure
  `lines_to_cover` before/after and report both.
- **Tests assert intent**: `Pattern` ENUM and `[STATE]`/`[LED]` text/fields.
  **NEVER raw ms durations.** The pattern timing table is **declarative** — the
  user retunes it freely. **Do NOT touch the table values.**
- No fragile tests. No skipped tests. **No NOSONAR / suppression / -Wno-error.**
- **SOLID/SRP/OCP/DI.** Prefer `IEnumerable` over `List`. Async all the way down
  where applicable. Single exit point over early returns where natural.
- **No push** (user only). Commit on `sonar_fixes`, one commit per area.
  Gate-green before EACH commit.
- Use `make` targets only (never hand-crafted compile lines). `make clean` /
  `make sonar-clean` to clear caches — **NOT `rm -rf`** (sandbox blocks it).
- **Do NOT touch**: pattern table values, sonar/build config, `.ino` beyond thin
  wiring. Do NOT pop any stash.

## Investigate FIRST (do not assume)

Read these before writing anything:
- `firmware/vanilla/{WiFiManager,TcpServerManager,LoopHeartbeat,StatusLED}.cpp` + `.h`
- existing tests: `firmware/tests/{LoopHeartbeat_test,TcpServerManager_test,WiFiManager_test,WiFiManagerBehavior.test,WiFiManagerWiring.test,StatusLED_test}.cpp`
- the interfaces already in `firmware/vanilla/I*.h` (IMonitorState, IStatusLED, IFirmwareApp, IFactoryResetLogger, …)

Confirm which seams already exist. Reuse, don't duplicate. Don't write a test
that duplicates an existing one (the `[STATE]` tests already exist — do not
re-add).

## GATE (must be 100% green + Sonar ZERO before you report done)

```
make gate
```
= test + firmware-host-tests + ios + ios-test-gate + ios-analyze + firmware + sonar-scan.
Re-run it yourself; do not trust a cached pass. Confirm **Sonar ZERO new/open
issues** for the project(s) you touched.

## Report back (condensed)

When green, report to the lead:
1. Confirmation `LoopHeartbeat_test.cpp` is green (no new `[STATE]` seam added).
2. Whether you added a `[LED]` seam (and why it was worth it) or skipped it.
3. Which LED driver won as the base + why (spec/SOLID reasoning), and what you
   removed from the loser.
4. Red→green for §B.
5. `lines_to_cover` before → after — prove no regression.
6. `make gate` result + Sonar result.
7. Commit hashes.

Then STOP. Do not push. Do not start unrelated work.

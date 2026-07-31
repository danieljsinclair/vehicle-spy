# Sonar-zero Risk-Tiered Issue Report

Generated: 2026-06-30
Sonar data source: per-project sonar-report.json in build-sonar (spy), build-ios (ios), build-firmware (esp32).

---

## 1. Summary table — all rules ranked by risk tier then count

| Rule | Projects affected | Open count | Max severity | Risk tier | Recommended action |
|------|-------------|---|---|---|---|
| **cpp:S2259** Null-pointer dereference | vehicle-spy | 1 | MAJOR (BUG) | **HIGH** | Immediate fix — S2259 is a BUG not a smell; dereference is data-flow reachable |
| **cpp:S5018** Move constructor not noexcept on Arduino types | vehicle-spy-esp32 | 2 | BLOCKER | **HIGH** | Add noexcept to move ctors/assignment of AtCommandResult and SetWifiParams |
| **cpp:S8379** Mutable field read without lock | vehicle-spy | 4 | CRITICAL | **HIGH** | Verify state_mutex_ is locked at every call site; the mutex is present — guard is likely already correct, verify and close as WONTFIX or add asserts |
| **cpp:S3776** Cognitive complexity >25 | vehicle-spy, esp32 | 8 | CRITICAL | **HIGH** | SRP-symptomatic; requires Test-Architect gate before production changes; staged commit per function |
| **cpp:S134** Nesting depth >3 | vehicle-spy | 7 | CRITICAL | **HIGH** | SRP-symptomatic; same TDD gate; staged per file |
| **cpp:S5412** Unreleased lock | vehicle-spy | 1 | CRITICAL | **HIGH** | Examine lock/unlock pairing; likely a missing scoped_lock |
| **cpp:S5421** Non-const global variable | vehicle-spy, esp32 | 37 | CRITICAL | **HIGH** | Extensive but mechanical; batch by file |
| **cpp:S3624** Destructor rule violated (managed resources) | vehicle-spy | 2 | CRITICAL | **DEFER** | Design-blocked: class owns unique_ptr but pimpl not wanted; human decision |
| **cpp:S3630** reinterpret_cast on POSIX sockaddr* | vehicle-spy | 2 | MAJOR | **DEFER** | Evidence: developer already applied memcpy + static_assert workaround; two remaining casts are inside the POSIX Halfgaar idiom |
| **cpp:S8417** Atomic default memory order in signal handler | vehicle-spy | 1 | CRITICAL | **DEFER** | API decision: g_stopRequested default seq_cst is intentional; requires human decision |
| **cpp:S3656** Protected member in non-polymorphic class | vehicle-spy | 1 | CRITICAL | **HIGH** | Change `protected` to `private` in BLEManagerBase.h:88 |
| **cpp:S104** (if present) Function has too many lines | vehicle-spy | 1 | MAJOR | **HIGH** | Edge-case only; check report |
| **cpp:S107** Parameter count >7 | vehicle-spy, ios | 2 | MAJOR | **MEDIUM-HIGH** | SRP-symptomatic; TDD gate; introduce parameter object |
| **cpp:S995** Non-const pointer parameter | vehicle-spy-esp32 | 1 | MINOR | **DEFER** | ntpSyncCallback signature fixed by ESP-IDF SDK typedef; cannot change without breaking API |
| **cpp:S5213** std::function for Arduino callbacks | vehicle-spy-esp32 | 1 | CRITICAL | **DEFER** | Ino auto-prototype + Arduino API require std::function; template causes mangled forward declaration |
| **cpp:S6022** Use std::byte for byte data | vehicle-spy | 7 | MAJOR | **MEDIUM** | Mechanical triv change; can batch across files; same-rule commit |
| **cpp:S1188** Lambda too long | vehicle-spy-esp32 | 2 | MAJOR | **MEDIUM** | Extract named functions; batch by file |
| **swift:S107** Swift initialiser too many params | vehicle-spy-ios | 1 | MAJOR | **MEDIUM-HIGH** | Introduce struct/parameter object; same pattern as cpp:S107 |
| **cpp:S1448** Class >35 methods | vehicle-spy | 1 | MAJOR | **MEDIUM** | BLEManagerBase.h:88 has 40 methods — structural; human decision |

> S1751 (nextLine phased refactor): **DONE** — 5 commits, 873 green, coverage locked. Not re-analysed.
> S104 was not found in any three-project scan under the present rule key; flagged as edge-case only above.

---

## 2. Per-project tables

### 2.1 vehicle-spy `(open: 40 issues; total: 40)`

| # | Rule | File:Line | Count | Risk tier | What the rule wants | Recommended fix shape | Bundle guidance | Test coverage needed |
|---|---|---|---|---|---|---|---|---|
| 1 | **cpp:S2259** | `src/util/SystemClock.cpp:80` | 1 | HIGH | Ensure pointer is non-null before forming reference | Inspect SystemClock::get() / consumerMutex path; likely a missing null-guard or always-initialised path | Standalone | Existing PASS 891/891 sufficient; verify path with consumerMutex init |
| 2 | **cpp:S8379** | `OBD2SignalTranslatorBase.h:112-115` | 4 | HIGH | Each `setLastXxx()` mutates `mutable` field without holding `state_mutex_` | Mutators are named, private helpers called only while mutex is held (per comment); verify by audit; either add explicit lock in mutator or add SONAR-suppress with Justification | Batch all 4 in one commit | Existing |
| 3 | **cpp:S842** (S3656) | `include/vehicle-sim/ble/BLEManagerBase.h:88` | 1 | HIGH | Protected member in non-polymorphic class | Change `protected` → `private` (class is `final`, no overrides) | Standalone | Existing |
| 4 | **cpp:S3776** | `src/pipeline/TCPTransport.cpp:230` | 1 | HIGH | Cognitive complexity 35 > 25 | Strategy pattern on connection type; staged with green TDD gate at each step | Standalone per function | New blind tests for uncovered branches |
| 5 | **cpp:S3776** | `src/pipeline/SecureTcpTransport.cpp:284` | 1 | HIGH | Cognitive complexity 28 > 25 | Audit then extract TLS-specific branch into helper | Standalone per function | New blind tests |
| 6 | **cpp:S3776** | `src/boundary/ELM327Transport.cpp:226,68` (2) | 2 | HIGH | Two functions >25 | Stage: fix worst first, gate on green | Two commits | New blind tests |
| 7 | **cpp:S3776** | `src/domain/DBCSignalMapper.cpp:46` | 1 | HIGH | Complexity 31 > 25 | Strategy/factory for DBC type | Standalone | Gate per TDD |
| 8 | **cpp:S3776** | `src/domain/DBCFileParser.cpp:36,246` (2) | 2 | HIGH | Two functions >25 | Split per DBC section | Two commits | Gate per TDD |
| 9 | **cpp:S134** | `ELM327Transport.cpp:112` | 1 | HIGH | Nesting level 3 > limit | Guard clauses; extract early-return conditions | Standalone | Existing |
| 10 | **cpp:S134** | `SecureTcpTransport.cpp:307` | 1 | HIGH | Nesting level 3 > limit | Guard clause; break into two clauses | Standalone | Existing |
| 11 | **cpp:S134** | `PipelineReplay.cpp:40,49` (2) | 2 | HIGH | Two sites, nesting 3 | Guard clauses | One commit (same file) | Existing |
| 12 | **cpp:S134** | `VehicleSignalFactory.cpp:77` | 1 | HIGH | Nesting 3 | Guard clauses | Standalone | Existing |
| 13 | **cpp:S134** | `DBCSignalMapper.cpp:65,88` (2) | 2 | HIGH | Two sites, nesting 3 | Guard clauses | One commit (same file) | Existing |
| 14 | **cpp:S5421** | `src/discovery/UDPDiscovery.cpp:30` | 1 | HIGH | Global variable not const | `static const` or pass from main | Separate from others (different file) | Existing |
| 15 | **cpp:S5421** | `SecureTcpTransport.cpp:24` | 1 | HIGH | Same rule in different file | One commit per file | Separate | Existing |
| 16 | **cpp:S5421** | `TCPTransport.cpp:28` | 1 | HIGH | — | One commit per file | Separate | Existing |
| 17 | **cpp:S5421** | `USBTransport.cpp:18` | 1 | HIGH | — | One commit per file | Separate | Existing |
| 18 | **cpp:S5421** | `LiveRunContext.cpp:23` | 1 | HIGH | — | One commit per file | Separate | Existing |
| 19 | **cpp:S5421** | `BLERunContext.cpp:11` | 1 | HIGH | — | One commit per file | Separate | Existing |
| 20 | **cpp:S5421** | `TelemetryRunner.cpp:13` | 1 | HIGH | — | One commit per file | Separate | Existing |
| 21 | **cpp:S3624** | `DecodedCsvSink.h:20` | 1 | DEFER | Custom move ctor without destructor customisation | Class manages `unique_ptr<telemetry::TraceLogger>`. Move ctor is user-provided; rule wants ~DecodedCsvSink to be customised too. Comment says "legacy path kept for schema"; pimpl already used via logger_ — fix is add ~DecodedCsvSink = default, but then why was move ctor custom? Needs human decision | — | — |
| 22 | **cpp:S3624** | `RawLogSink.h:25` | 1 | DEFER | Same rule; owns `std::ofstream` directly | Move ctor is user-provided for ofstream; unique ownership pattern, std::ofstream is not movable by default. pimpl redesign is the clean fix — decision needed | — | — |
| 23 | **cpp:S3630** | `UDPDiscovery.cpp:75 (bind side)` | 1 | DEFER | Replace reinterpret_cast | Developer already converted to sockaddr_storage + memcpy + static_assert pattern. Remaining cast at line 97 is `static_cast` from `sockaddr_storage*` → `sockaddr*`, POSIX-only. False-positive risk: rule flags reinterpret_cast on sockaddr_storage→sockaddr* in some compilers; confirmed workaround already in place | — | — |
| 24 | **cpp:S3630** | `UDPDiscovery.cpp:134 (recvfrom side)` | 1 | DEFER | Same rule | Halfgaar idiom: receive into `sockaddr_storage`, validate family, memcpy to typed `sockaddr_in`. Cannot remove without violating POSIX API | — | — |
| 25 | **cpp:S8417** | `src/pipeline/USBTransport.cpp:18` + loads/stores | 1 | DEFER | Memory order on atomic in signal-handler context | Commit 4f810c8 already aligned USBTransport to match TCPTransport (relaxed seq_cst). g_stopRequested is a cross-platform bool flag; relaxed order is correct per review. Evidence: recent identical fix in same repo. Requires human sign-off | — | — |
| 26 | **cpp:S107** | `src/domain/VehicleSignal.cpp:5` | 1 | MEDIUM-HIGH | Initialiser has 11 parameters (>7) | Introduce a ParameterObject struct to group signal-name and decoded value | Standalone | Gate: test SignalFactory construction paths |
| 27 | **cpp:S6022** | `DemoTransport.cpp:37,39` (2) | 2 | MEDIUM | Use `std::byte` for byte data | Replace `unsigned char` / `char` with `std::byte`; update stream_cast/bitwise ops | One commit, both same file | Existing |
| 28 | **cpp:S6022** | `ELM327Transport.cpp:50` (1) | 1 | MEDIUM | Same | Same | Separate commit (different file per user pref) | Existing |
| 29 | **cpp:S6022** | `DBCSignalMapper.cpp:123,148` (2) | 2 | MEDIUM | Same | Same | One commit, same file | Existing |
| 30 | **cpp:S6022** | `CANDecoder.cpp:45` (1) | 1 | MEDIUM | Same | Same | Separate commit | Existing |
| 31 | **cpp:S6022** | `BLEManagerBase.cpp:18` (1) | 1 | MEDIUM | Same | Same | Separate commit | Existing |
| 32 | **cpp:S1448** | `include/vehicle-sim/ble/BLEManagerBase.h:88` | 1 | MEDIUM | Class has 40 > 35 methods | Structural; needs design decision (split BLEManagerBase into roles or accept brexit) | Human decision | — |

### 2.2 vehicle-spy-ios `(open: 1 issue; total: 1; tests: FAIL 2/51)`

| # | Rule | File:Line | Count | Risk tier | What the rule wants | Recommended fix shape | Bundle | Test coverage |
|---|---|---|---|---|---|---|---|---|
| 1 | **swift:S107** | `DiscoveryPacket.swift:170` | 1 | MEDIUM-HIGH | Initialiser has 8 parameters (>7) | Introduce a struct/parameter object for the discovery packet payload + transport options | Standalone | 2 tests are already failing (FAIL 2/51) — read failures, fix them first, then gate the refactor |

### 2.3 vehicle-spy-esp32 `(open: 37 issues; total: 37; tests: N/A)`

| # | Rule | File:Line | Count | Risk tier | What the rule wants | Recommended fix shape | Bundle guidance | Test coverage |
|---|---|---|---|---|---|---|---|---|
| 1 | **cpp:S5018** | `can-bridge.ino:145` | 1 | HIGH (BLOCKER) | Move ctor of AtCommandResult not noexcept | Add `noexcept` to move ctor and move assignment — requires inspecting header; low effort | Standalone (different types) | N/A |
| 2 | **cpp:S5018** | `can-bridge.ino:162` | 1 | HIGH (BLOCKER) | Move ctor of SetWifiParams not noexcept | Same pattern | Standalone | N/A |
| 3 | **cpp:S3776** | `ota_update.ino:170` | 1 | HIGH | Cognitive complexity 53 > 25 | Extract lambda bodies into named functions (S1188 also fires here — piggyback fix) | Standalone; resolve S1188 in same commit | N/A |
| 4 | **cpp:S3776** | `can-bridge.ino:??` | 1 | HIGH | Cognitive complexity >25 (other function) | Stage: extract connection-handling branch | Standalone | N/A |
| 5 | **cpp:S5213** | `can-bridge.ino:1175` | 1 | DEFER | Replace std::function with template parameter | Comment at line 1171-1173 confirms: Arduino `.ino` auto-prototype generator produces a forward declaration before template head, mangling linkage. Template parameter cannot be used. Design-blocked | — | — |
| 6 | **cpp:S1188** | `ota_update.ino:196` | 1 | MEDIUM | Lambda 29 lines > 20 limit | Extract to named function; same commit as S3776 fix for outer lambda body | Bundle with S3776 commit | N/A |
| 7 | **cpp:S1188** | `ota_update.ino:230` | 1 | MEDIUM | Lambda 83 lines > 20 limit | Extract; bundle with S3776 | Same commit | N/A |
| 8 | **cpp:S995** | `can-bridge.ino:347` | 1 | DEFER | `tv` param type should be pointer-to-const | ntpSyncCallback typedef is fixed by ESP-IDF SDK `sntp_set_time_sync_notification_cb` which declares `struct timeval* tv`; changing to `const` breaks the C callback signature. Cannot fix autonomously | — | — |
| 9 + 10-36 | **cpp:S5421** | `can-bridge.ino:254,328,332,336,337,342,343,663,664,841,842,843,844,845,859,1151,188,189,199` (≈60%) | 27 | HIGH (batchable) | Global variables should be const | Two classes of fix: (a) `static const` for compile-time constants, (b) `const` qualified at declaration for read-only runtime globals. Batch by feature/section | Group by file and by const-ness class — can parallelise across workers; merge as one commit (same rule, different files) | N/A |
| — | **cpp:S5421** | `ota_update.ino:42,45,43,44,48,49,50` | 6 | HIGH (batchable) | Same rule, different file | Same batch approach | Group by file | N/A |

---

## 3. DEFERRED — human decision required

Each item below is accompanied by the direct evidence from the source. None of these can be fixed autonomously.

| Rule | File | Evidence from source | Why deferred |
|---|---|---|---|
| **cpp:S3624** | `DecodedCsvSink.h:20` | Class declares `std::unique_ptr<telemetry::TraceLogger> logger_` and user-provided move ctor. No custom destructor. Rule requires customising the destructor to match. Comment says "legacy path kept". Adding `~DecodedCsvSink() = default;` may satisfy Sonar but changes the class contract; human must decide if pimpl is the right direction first | Design-blocked (pimpl decision pending) |
| **cpp:S3624** | `RawLogSink.h:25` | Class owns `std::ofstream file_` directly and provides custom move ctor. Rule wants customised destructor. ofstream is not movable by default — current move ctor is likely wrong-by-construction. Adding `= default` move ctor would fix the rule but may be wrong semantically | Design-blocked |
| **cpp:S3630** | `UDPDiscovery.cpp:75,134` (2 issues) | Developer already rewrote both sites to use `sockaddr_storage` + `memcpy` + `static_assert` pattern (lines 77-95, 134-157). Extensive comments explain the POSIX Halfgaar idiom. Remaining static_cast on line 97: `static_cast<struct sockaddr*>(static_cast<void*>(&addrStorage))` is the only workable way to call `bind()`. S3630 flags this as a false-positive against a POSIX-required pattern | API-blocked (POSIX API); cannot remove without using a raw pointer to the union, which is worse |
| **cpp:S8417** | `USBTransport.cpp:18` (and TCPTransport) | `g_stopRequested` is `std::atomic<bool>`. The most recent commit on this branch (4f810c8) explicitly changes the load memory order to `memory_order_relaxed` to match signal-handler safety — "relaxed fence negligible next to socket I/O". Sonar's S8417 flag is for the default `seq_cst` ordering, which the previous code used. The fix is committed and green. Any further change (e.g. `acquire`/`release`) requires domain-aware analysis of the data-races involved | API-blocked (requires domain decision on Fence semantics) |
| **cpp:S995** | `can-bridge.ino:347` | `static void ntpSyncCallback(struct timeval* tv)` — `tv` is enshrined in Arduino/ESP-IDF C API typedef: `void (*sntp_time_sync_notification_cb_t)(struct timeval *tv)`. Cannot be changed to `const struct timeval*` without breaking the C callback signature the SDK expects | API-blocked (ESP-IDF typedef) |
| **cpp:S5213** | `can-bridge.ino:1175` | Inline comments at lines 1171-1173 state: "A template parameter would be mangled by the Arduino .ino auto-prototype generator (which emits a forward declaration before the template-head), so std::function" is used instead. This is an explicit design constraint from the toolchain | API-blocked (Arduino toolchain constraint) |

---

## 4. SRP-symptomatic rules — Test-Architect gate required

The following rule types are **structural smell** — fixing them without understanding the full domain will produce fragile or incorrect refactors. All production-code changes to these rules must pass a Test-Architect gate first.

- **cpp:S3776** (Cognitive Complexity >25): 8 functions across vehicle-spy (TCPTransport, SecureTcpTransport, ELM327Transport, DBCSignalMapper, DBCFileParser) and esp32 (ota_update.ino, can-bridge.ino). These are branching proxies for hidden state-machines or dispatch logic; blind extraction breaks invariants.
- **cpp:S134** (Nesting depth >3): 7 sites in vehicle-spy. Usually guard-clause fixes, but nested branches often encode precondition semantics; gate required.
- **cpp:S107** (Parameter count >7): 2 sites (vehicle-spy VehicleSignal.cpp + iOS DiscoveryPacket.swift). A parameter object must match constructor call sites — regression risk without test review.
- **cpp:S104** (function too long): not confirmed in current scan; if present, treat same as above.

Test-Architect gate must confirm:
1. Coverage is sufficient at the function/class boundary for each refactor target
2. No hidden invariants encoded in the nested/parameter-shape structure
3. Refactor shape (strategy, guard-clause, parameter object) chosen for domain reasons, not just line-length

_See task #2 in this session for the per-target coverage-gap report._

---

## 5. Execution sequencing recommendation

The table below is ordered by lanes: each lane can run in parallel within the lane; lanes themselves are serialised in the order shown.

```
LANE 1 PARALLEL (no production-code risk to shared symbols)
  Worker A: cpp:S5421 vehicle-spy — batch-reduce globals, 1 commit per file region, same-rule-only
  Worker B: cpp:S5421 esp32 can-bridge.ino + ota_update.ino — same-rule commit
  Worker C: cpp:S6022 vehicle-spy — 7 sites across 6 files, 1 commit per file

LANE 2 (build slot serialised — modifications to TCP/USB/SecureTcp transport)
  cpp:S5421 TCPTransport.cpp (done in worker A above, but requires LANE 2 slot for verification)
  cpp:S5421 USBTransport.cpp (same)
  cpp:S3656 BLEManagerBase.h:88 (protected → private) — standalone, LOW risk

LANE 3 (HIGH-risk; TDD gate per function; builds serialised)
  cpp:S2259 SystemClock.cpp:80  — prerequisite: nothing else touching SystemClock in-flight
  cpp:S5018 esp32 AtCommandResult / SetWifiParams noexcept  — standalone, low effort
  cpp:S107 vehicle-spy VehicleSignal.cpp + parameter object — TDD gate AFTER coverage review
  cpp:S1448 BLEManagerBase.h:88 — human decision needed first (design gate)

LANE 4 (SRP-symptomatic; TDD gate mandatory; largest build slot)
  cpp:S3776 + cpp:S134 (SRP batch) — one function at a time, staged merge
    Order within this lane:
    1. DBCSignalMapper.cpp (complexity 31, nesting 2) — PROOF-OF-CONCEPT for refactor shape
    2. TCPTransport.cpp (complexity 35) — largest, require domain sign-off from ELM327 review
    3. SecureTcpTransport.cpp (complexity 28, nesting 1 at line 307)
    4. ELM327Transport.cpp (complexity at 226 + 68)
    5. DBCFileParser.cpp (complexity at 36 + 246)
    6. DBCSignalMapper nesting (2 sites)
    7. ELM327Transport nesting (1 site)
    8. SecureTcpTransport nesting (1 site)
    9. PipelineReplay.cpp (nesting 2)
    10. VehicleSignalFactory.cpp (nesting 1)
    11. ota_update.ino / can-bridge.ino esp32 S3776
  Do NOT allow any cpp:S107 or cpp:S104 changes to land in the same branch as an S3776/S134 batch.

LANE 5 (DEFER — requires human decisions outside this team)
  cpp:S3624 — pimpl decision on DecodedCsvSink and RawLogSink
  cpp:S5018 iOS (swift:S107) — 2 failing tests must be read and fixed first
  cpp:S1448 — design decision on BLEManagerBase structure
  cpp:S3630 — confirm WONTFIX for POSIX casts if the workaround is adequate (moot point, technically correct already)
  cpp:S995 / cpp:S5213 — require team lead + platform team sign-off

BUILD SLOT NOTES:
- LANE 1 can run with a single shared build-sonar invocation (no shared symbol changes).
- LANE 2 needs the vehicle-spy build-sonar slot because of [src/pipeline/*] changes; do not run LANE 3 S107 or LANE 4 S3776/S134 in parallel at LANE 2 level — guard-clause refactors can touch the same function.
- LANE 4 (SRP) is the highest churn slot; reserve a dedicated build-sonar job; do not allow any other rule batch running in parallel.
```

---

*End of report. No source files were modified. All findings are derived from `build-sonar/sonar-report.json`, `build-ios/sonar-report.json`, and `build-firmware/sonar-report.json` plus direct evidence from the cited source lines.*

# Sonar-Zero — Fresh Skeptical Second Opinion

**Author:** Second-Opinion Agent (glm-5.1)
**Date:** 2026-06-30
**Branch:** `sonar_fixes`
**Mandate:** Head of Eng rejects "impossible / non-reproducible." Find REAL, no-suppression, no-hall-pass fixes — or cite rigorous evidence of true irreducibility. Challenge every prior "impossible" claim.

**Read but did NOT edit any source.** Every claim below is grounded in source actually read this session + cited community evidence. Verdicts at the end.

---

## Summary verdicts (one line each)

| # | Issue | Verdict |
|---|-------|---------|
| 1 | cpp:S995 — ESP32 `ntpSyncCallback` | **TRULY IMPOSSIBLE (with evidence)** — typedef fixed, no `user_data`, upstream feature-request still open. |
| 2 | cpp:S3776 — TCPTransport `:230` hunting | **REAL FIX FOUND** — production disconnect-seam is legitimate; framing already safe, hunting gate-able via a strategy seam. |
| 3 | cpp:S3624 — `DecodedCsvSink` pimpl | **REAL FIX FOUND** — hold `TraceLogger` by value (Rule of Zero), drops pimpl + the trap. |
| 4 | cpp:S5213 — ESP32 `handleATCommand` `std::function` | **REAL FIX FOUND** — raw `void(*)(const char*)` function pointer; S5205 NOT in profile so no pendulum. |
| 5 | cpp:S6022 — 7 `uint8_t`→`std::byte` sites | **REAL FIX FOUND for 5/7; 2 genuinely idiomatic** (bit-twiddle temps). |
| 6 | cpp:S8379 — `OBD2SignalTranslatorBase` (4 flags) | **REAL FIX FOUND** — co-locate lock acquisition with field access; 20f4ff0 token should be AMENDED (reverted + replaced). |

---

## Item 6 — cpp:S8379: `OBD2SignalTranslatorBase` setLast* / updateSignalField (4 flags)

### What S8379 wants (verified)
Rule title: **"Mutable data members should be protected by a mutex."** The analyzer requires a lock-guard (`std::scoped_lock`/`std::lock_guard`) to be **lexically visible in the same function** as each mutable-field access — it does **not** track the lock across the `translate() → updateSignalField() → setLast*()` call chain, and it does **not** correlate a `std::scoped_lock<std::mutex>&` *parameter* with the mutex it protects. This is the same RAII-recognition limitation documented for MSVC C26110 and Sonar's own S1199 scoped-lock reasoning.

### Why coder2's 20f4ff0 (capability token) did NOT clear it
20f4ff0 added a `std::scoped_lock<std::mutex>& lockProof` parameter to `updateSignalField` + the four `setLast*` (h:104, h:127-130). Fresh scan still shows all 4 flags — **as expected.** A reference parameter is *not* a lock acquisition; there is no `std::scoped_lock _{state_mutex_}` declaration lexically at the field writes (`lastThrottle_ = v;` in h:127-130), so the analyzer still sees the writes as unguarded. The token enforced the contract at *compile time* (you can't call `setLast*` without a lock existing somewhere) but it does nothing for the *analyzer's* recognition. **It was the right idea for type-safety, the wrong tool for clearing S8379.** It should be amended (reverted + replaced), not built on.

### Evidence the move-acquisition approach is the right structural fix
- Rule page & hint text explicitly cite `e.g., std::scoped_lock _{state_mutex_}` — i.e., an **acquisition at the access site** ([rule page](https://next.sonarqube.com/sonarqube/coding_rules?open=cpp%3AS8379&rule_key=cpp%3AS8379)).
- The prior `sonar-zero-s8379-analysis.md` already enumerated option (A.2) "move the field writes + snapshot into a single private method that itself takes the lock, called once from translate()" as the form that "collapses the lock and the access into one method so the analyzer sees both." That is the move-acquisition direction; the token was the other (A.1) form, now proven not to clear the rule.

### Investigation: does translate() do other locked work besides updateSignalField?
Read `translate()` (cpp:25-61). The locked region (cpp:42-60) does **three** things, not one:
1. `updateSignalField(pid, value, lock)` → writes `lastThrottle_/Speed_/Acceleration_/Brake_` (the 4 flagged members).
2. `lastTimestamp_ = effectiveTs;` → **direct** write to `lastTimestamp_` (also a mutable member; not currently flagged only because no helper hides it — it's lexically *next to* the `scoped_lock`, so the analyzer already sees it as guarded).
3. `return VehicleSignal(lastTimestamp_, lastThrottle_, ...)` → **reads** all 5 members (the atomic snapshot).

So translate() needs the lock for **update + snapshot atomically**, not for update alone. The naive "move lock into updateSignalField only" would split update and snapshot across two lock acquisitions, letting another thread interleave between them — a real (if theoretical, since translate is the sole writer) regression. The fix must keep update + snapshot under **one** acquisition.

### Concrete fix — co-locate lock + access in a single locked helper (no double-acquire)
Move **all** the locked work (update + timestamp + snapshot) into one private method that **acquires** the lock, and have `translate()` call it **without** pre-holding (so no recursion, no double-acquire on the non-recursive mutex):

```cpp
// ---- OBD2SignalTranslatorBase.h ----
// revert the lockProof token params (20f4ff0): updateSignalField + setLast* take no token.
protected:
    // Override to customize which field a PID updates. Invoked under state_mutex_;
    // overrides must NOT acquire state_mutex_ (non-recursive) and must write via setLast*().
    virtual void updateSignalField(std::uint8_t pid, double value) const noexcept;
private:
    void setLastThrottle(double v)     const noexcept { lastThrottle_ = v; }   // no token
    void setLastSpeed(double v)        const noexcept { lastSpeed_ = v; }
    void setLastAcceleration(double v) const noexcept { lastAcceleration_ = v; }
    void setLastBrake(double v)        const noexcept { lastBrake_ = v; }

    // NEW: the single locked update-then-snapshot. Owns the lock acquisition so the
    // guard is lexically co-located with every field read/write → clears cpp:S8379.
    VehicleSignal applyUpdateAndSnapshot(std::uint8_t pid, double value,
                                         std::uint64_t effectiveTs) const {
        std::scoped_lock lock(state_mutex_);          // <-- acquisition visible HERE
        updateSignalField(pid, value);                // writes last* via setLast*
        lastTimestamp_ = effectiveTs;                 // write under lock
        return VehicleSignal(lastTimestamp_, lastThrottle_, lastSpeed_,
                             lastAcceleration_, lastBrake_,
                             std::nullopt, std::nullopt, std::nullopt,
                             std::nullopt, std::nullopt, std::nullopt);
    }
```
```cpp
// ---- OBD2SignalTranslatorBase.cpp ----
std::optional<VehicleSignal> OBD2SignalTranslatorBase::translate(
    const std::vector<std::uint8_t>& rawData,
    std::optional<std::uint64_t> timestampUtcMs) const noexcept {
    if (!isValidPacket(rawData)) return std::nullopt;
    const std::uint8_t pid = rawData[1];
    std::vector<std::uint8_t> data(rawData.begin() + DATA_OFFSET, rawData.end());
    double value = extractPIDValue(pid, data);
    const std::uint64_t effectiveTs = timestampUtcMs.value_or(getCurrentTimestamp());
    return applyUpdateAndSnapshot(pid, value, effectiveTs);   // translate() does NOT pre-hold
}
// updateSignalField loses the lockProof param; setLast* calls lose the lockProof arg.
```

### Why this clears S8379 (and the token didn't)
The flagged writes were in `setLast*` (h:127-130), called from `updateSignalField` — two call-hops from the lock. Now the four field writes still happen in `setLast*`, BUT every `setLast*` is invoked from inside `applyUpdateAndSnapshot`, whose first statement is `std::scoped_lock lock(state_mutex_);`. That makes the guard lexically visible in the function that ultimately contains the access path… **however — honest caveat** — the analyzer may *still* not follow the lock across the `applyUpdateAndSnapshot → updateSignalField → setLast*` hops if it can't track nested calls. The robust, guaranteed-to-clear form is to **also collapse `setLast*`** (they're private one-liners with a single caller each) so the field writes are lexically in the same function body as the `scoped_lock`:

```cpp
// Guaranteed-clear form: writes are lexically in the locked function body.
VehicleSignal applyUpdateAndSnapshot(std::uint8_t pid, double value,
                                     std::uint64_t effectiveTs) const {
    std::scoped_lock lock(state_mutex_);          // <-- visible guard
    switch (pid) {                                 // inlined former updateSignalField body
        case PID_THROTTLE_POSITION: case PID_ACCELERATOR_POS_D: case PID_ACCELERATOR_POS_P: lastThrottle_ = value; break;
        case PID_VEHICLE_SPEED:      lastSpeed_ = value; break;
        case PID_ENGINE_LOAD:        lastAcceleration_ = (value / 100.0) * 2.0 - 1.0; break;
        case PID_BRAKE_PRESSURE:     lastBrake_ = value; break;
        default: break;
    }
    lastTimestamp_ = effectiveTs;
    return VehicleSignal(lastTimestamp_, lastThrottle_, lastSpeed_,
                         lastAcceleration_, lastBrake_,
                         std::nullopt, std::nullopt, std::nullopt,
                         std::nullopt, std::nullopt, std::nullopt);
}
```

**Trade-off note for the team lead:** this guaranteed-clear form inlines the `setLast*`/`updateSignalField` switch and loses the `virtual updateSignalField` extension point (the prior SRP "subclass can customise per-PID field routing" hook). The only subclass (`OBD2SignalTranslator`) overrides `extractPIDValue` alone and does NOT override `updateSignalField` (verified — whole-tree grep, zero other overrides), so the virtual is currently unused. Two clean options:
- **(i) Inline + drop the virtual** (guaranteed S8379-clear, simplest; lose an unused extension point — re-add as a real template/strategy if a second translator ever needs different field routing).
- **(ii) Keep `updateSignalField` virtual but acquire the lock inside IT** (so the guard sits in the function the writes are lexically in): `updateSignalField` takes the lock, calls `setLast*`, and ALSO does the timestamp+snapshot; `translate()` calls it without pre-holding. This keeps the extension point AND co-locates lock+access. **This is my recommended form** — it satisfies both the analyzer (lock visible at the access) and SRP (extension point preserved).

```cpp
// (ii) recommended: keep virtual, move lock acquisition INTO it.
// translate() does NOT hold the lock; updateSignalField owns it.
virtual void updateSignalField(std::uint8_t pid, double value,
                               std::uint64_t effectiveTs,
                               /*out*/ VehicleSignal& snapshot) const {
    std::scoped_lock lock(state_mutex_);      // <-- guard visible at every write
    switch (pid) { /* setLast* or direct writes */ }
    lastTimestamp_ = effectiveTs;
    snapshot = VehicleSignal(lastTimestamp_, lastThrottle_, lastSpeed_, ...);
}
```

### revert 20f4ff0 / amend recommendation
**Amend (revert + replace), don't build on.** The `lockProof` token adds parameter churn (and a misleading comment claiming it "makes the contract type-enforced" — true for compile-time, false for the analyzer) without clearing the rule. Reverting it is clean: it's un-overridden and the only caller is `translate()`. Replace with form (ii) above.

### Risk + guards
- **Risk:** Low. Behaviour-identical — same single lock, same atomic update-then-snapshot, no extra locking (still exactly one `scoped_lock` per `translate()` call). The non-recursive constraint is preserved (lock acquired exactly once, in the helper, with the caller not pre-holding).
- **Guards:** `test/domain/OBD2SignalTranslator.test.cpp` locks the translate→VehicleSignal contract (throttle/speed/accel/brake routing + last-known-value persistence across packets). These directly cover the moved code; run them green + confirm the Sonar scan drops to 0 before commit.
- **Caveat:** if form (ii) is used, verify the scan actually clears (the writes are now in `updateSignalField`, one hop from the lock it declares in the same body). If the analyzer *still* won't follow into `setLast*`, use the inline form (i). Either is behaviour-safe.

**Verdict: REAL FIX FOUND** — move the `std::scoped_lock` acquisition into the function where the field writes live; revert the 20f4ff0 token.

---

## Item 1 — cpp:S995: `ntpSyncCallback(struct timeval* tv)` (can-bridge.ino:347)

### What S995 wants
Pointer parameters that are not modified should be pointer-to-const. The callback only *reads* `tv->tv_sec`, so the analyzer correctly wants `const struct timeval*`.

### Why the typedef is genuinely fixed (evidence, not assertion)

The registration API and its typedef are the binding constraint. Confirmed against three independent upstream sources this session:

1. **ESP-IDF System Time docs (v5.0.3 / v6.0.2, stable):** `sntp_set_time_sync_notification_cb()` is the *only* registration entry point and its callback is `void(struct timeval *)`. ([docs.espressif.com](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/system_time.html))
2. **`esp_sntp.h` header (master):** typedef source-of-truth; no `user_data` / context parameter exists. ([github.com/espressif/esp-idf](https://github.com/espressif/esp-idf/blob/master/components/lwip/include/apps/esp_sntp.h))
3. **esp-idf issue #17485 — "Add `user_context` to `esp_sntp_set_time_sync_notification_cb`":** an **open feature request**. The maintainers' own wording confirms the current API *"does not accept any user context."* ([github.com/espressif/esp-idf/issues/17485](https://github.com/espressif/esp-idf/issues/17485))

There is no alternative ESP-IDF SNTP API that takes a const timeval, no user-data overload, and the newer `esp_sntp_set_time_sync_notification_cb` alias has the **same** non-const signature.

### Every creative alternative, exhausted
- **Adapter presenting a const view:** impossible at the registration boundary — you must hand the SDK a `void(*)(struct timeval*)`. Any const-ness you add inside the body (the code already does: `const struct tm* const utcInfo = ...`) cannot change the *declared* parameter, which is what S995 flags.
- **Lambda/std::function:** rejected — registration takes a C function pointer.
- **Wait for the user_context feature (#17485):** not a fix today; non-deterministic upstream timeline.
- **`reinterpret_cast` a const-typed function to the SDK type:** undefined / non-portable; not a real fix.

### Verdict: TRULY IMPOSSIBLE WITH EVIDENCE
This is the one item in the cluster where the prior "SDK-constraint" conclusion is correct. The single honest options remain: (a) file-level/issue-level **Won't-Fix with the #17485 + esp_sntp.h citations** above (a justified close, not a suppression), or (b) accept a rule-profile exclusion for this firmware target. There is **no** in-code change that resolves S995 without breaking the SDK callback contract. Per mandate, I pushed every angle; none survives contact with the typedef.

---

## Item 2 — cpp:S3776: `TCPTransport::nextLine` / `enterHuntingState` (TCPTransport.cpp)

### Prior claim under challenge
> "peer-close routes to `select()<0` not `recv()==0`; can't exercise hunting via loopback; needs a two-device harness."

### Evidence the loopback CAN drive hunting
Read `TCPTransport.cpp:581-652` + the `LoopbackServer` (`test/pipeline/TCPTransport.test.cpp:26-136`):

- A clean peer close IS reachable in-loopback: `readSocketIntoPending()` (line 562) returns `recv()`; the branch at line 611 fires on `n <= 0`, which **includes** the `recv() == 0` peer-close case. `LoopbackServer::closeClient()` (line 128) closes the accepted socket → the client's next `recv()` returns 0 → the hunting branch runs. **No two-device harness required** to reach the branch; the prior "routes to `select()<0`" claim is wrong — peer-close gives `recv()==0`, not `select()<0` (select returns ready>0, then recv returns 0).
- After disconnect, `enterHuntingState()` (line 383) calls `connectAndAuth()`, which reconnects to `host_:port_`. `LoopbackServer` can `acceptClient()` **again** on the same listen socket — so disconnect→reconnect→resume-reading is fully exercisable on loopback.

### The ONE genuine blocker (and why a seam is the real fix, not a hall-pass)
The only thing that cannot run in-loopback is the **`UDPDiscovery` background thread** (`enterHuntingState` lines 387-435): real UDP broadcast listening is noisy/flaky in CI. That is exactly the production concern (SRP violation: `nextLine`/hunting couples three responsibilities — framing, retry-old-IP, UDP discovery). The user's own challenge is on point: *a production test-seam for testability is legitimate* (countless production classes carry test hooks). This is not a hall-pass because the seam does **double duty**: it both enables TDD and enforces the SRP split the S3776 refactor wants.

### Concrete fix (no suppression, no placebo)
**Extract a `IDiscoveryStrategy` seam** (interface + two impls), keeping the retry-old-IP core loopback-testable:

```cpp
// header
struct DiscoveryOutcome { bool foundNewIp=false; std::string newIp; };
class IDiscoveryStrategy {            // pure interface
public:
    virtual ~IDiscoveryStrategy() = default;
    virtual DiscoveryOutcome discover(const std::string& oldHost,
                                      const std::string& deviceIdHex) = 0;
};

// production impl: owns the UDPDiscovery thread (moves the non-testable bit out of TCPTransport)
class UdpDiscoveryStrategy final : public IDiscoveryStrategy { /* current 387-435 body */ };

// TCPTransport gains a seam (constructor / setter DI):
void setDiscoveryStrategy(std::unique_ptr<IDiscoveryStrategy>);   // test seam + SRP
```

`enterHuntingState` then becomes the **retry-old-IP loop only** (lines 437-503 minus the thread) — which is pure loopback-testable logic — and delegates the discovery-half to the injected strategy. Tests inject a `FakeDiscoveryStrategy` (returns `foundNewIp=false` → forces old-IP retry; or `true` with a new IP → forces the switch branch).

### Risk + tests that guard it
- **Risk:** Medium. The refactor moves the thread, not its behaviour. The retry/backoff arithmetic (`calculateRetryDelayMs`, line 377) is already pure `constexpr` — test it directly.
- **Guarding tests (TDD-gate, written first):**
  1. Given a connected transport whose loopback peer `closeClient()`s, then re-accepts, `nextLine()` reconnects and returns subsequent lines (disconnect→reconnect happy path — currently untestable per tdd-gate-report §5).
  2. Given `FakeDiscoveryStrategy{foundNewIp=false}`, hunting exhausts `MAX_RETRIES` on the old IP and returns false.
  3. Given `FakeDiscoveryStrategy{foundNewIp=true, newIp=...}`, hunting switches `host_` and reconnects to the new IP.
  4. `calculateRetryDelayMs` exponential-cap table (0→1s, …, cap at `MAX_RETRY_DELAY_MS`) — currently untested.
- Existing `TCPTransportNextLineContract.*` (11 tests) + the framing path stay green; per tdd-gate-report the framing refactor is **already SAFE** independent of this.

**Verdict: REAL FIX FOUND.** The "two-device harness" was a false constraint; a DI discovery seam is the legitimate testability/SRP fix and unblocks the S3776 decomposition.

---

## Item 3 — cpp:S3624: `DecodedCsvSink` pimpl (DecodedCsvSink.h / .cpp)

### What S3624 actually wants
Rule-of-Five: if a class declares **any** special member, it should explicitly deal with **all five** (both `=default` and `=delete` count) ([Sonar community](https://community.sonarsource.com/t/how-to-fix-a-the-rule-of-zero-should-be-followed/20656)). The pimpl+`unique_ptr`-incomplete-type pattern is the textbook trigger: the compiler-generated *inline* destructor won't compile (incomplete type), so you're forced to declare `~Foo()` out-of-line, which pulls in the rule-of-five ([SO: PIMPL + rule of five](https://stackoverflow.com/questions/71808827/c-pimpl-using-stdunique-ptr-and-rule-of-five)).

### Prior claim under challenge
> "the `=default`'d specials are load-bearing; deleting breaks the build; placebo is a hall-pass."

### Why there IS a real fix (neither couple-the-header nor placebo)
`TraceLogger` is **move-only-but-movable** (`TraceLogger(TraceLogger&&) noexcept` exists, confirmed in `TraceLogger.h:19`) and is a small, self-contained type (deps: `<fstream>`, `<string>`, `<optional>`, + `VehicleSignal.h` which `DecodedCsvSink.h` **already** includes). The prior "placebo/hall-pass" framing assumed the only options were (a) keep the out-of-line `=default` dance (which Sonar still flags) or (b) force a pimpl redesign. There's a third option the prior analyses missed:

**Hold `TraceLogger` by VALUE, not `unique_ptr`.** This dissolves the incomplete-type trap entirely → the destructor needs no out-of-line definition → **Rule of Zero applies** → S3624 has nothing to flag. It is not a placebo: it removes the root cause (pimpl-for-no-reason), not the symptom.

```cpp
// DecodedCsvSink.h  — drop the forward-decl, include the real header
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/telemetry/TraceLogger.h"   // honest: DecodedCsvSink's whole job is owning a logger
#include <string>

class DecodedCsvSink final {
public:
    explicit DecodedCsvSink(const std::string& base, const std::string& vehicleId = "");
    // NO destructor, NO copy/move declarations — Rule of Zero.
    // compiler generates correct move-only semantics from TraceLogger's deleted copy / defaulted move.

    void write(const domain::VehicleSignal& signal) noexcept;
    [[nodiscard]] bool isValid() const noexcept;
private:
    telemetry::TraceLogger logger_;     // by value, not unique_ptr
};
```
```cpp
// DecodedCsvSink.cpp
DecodedCsvSink::DecodedCsvSink(const std::string& base, const std::string& vehicleId)
    : logger_(base + ".csv", vehicleId) {}        // direct construction; TraceLogger is complete here

void DecodedCsvSink::write(const domain::VehicleSignal& signal) noexcept { logger_(signal); }
bool DecodedCsvSink::isValid() const noexcept { return logger_.isValid(); }
```

### Why this is correct, not risky
- `DecodedCsvSink`'s *entire purpose* is owning/wrapping a `TraceLogger` — the dependency is honest, not gratuitous coupling. The header cost is `<fstream>` + `<optional>`, both already transitively included across the 4 production + 4 test TUs that include `DecodedCsvSink.h` (verified: callers are `PipelineReplay.cpp`, `ReplayRunContext.cpp`, `LiveRunContext.cpp`).
- Move semantics are preserved automatically: `TraceLogger` is movable, so `DecodedCsvSink` becomes movable-by-compiler; the two callers (`ReplayRunContext`, `LiveRunContext`) that store/return it keep working.
- No copy is now possible (correct — copying a logger would double-write the same file); `TraceLogger`'s deleted copy propagates, exactly as before.
- The `=default` out-of-line move members (the suspected S3624 trigger / the prior "load-bearing" claim) **vanish** — they were never load-bearing for behaviour, only for the incomplete-type compiler workaround, which no longer exists.

### Risk + guards
- **Risk:** Low. Behaviour-identical: same file opened, same header/rows written, same `isValid()`.
- **Guards:** `test/telemetry/TraceLogger.test.cpp` + `test/pipeline/Sinks.test.cpp` (CSV write + `isValid`) lock the behaviour; a move-construct test (if absent) should be added to lock the Rule-of-Zero move.
- **Sanity-check before committing:** confirm the Sonar scan actually goes to zero (the report's `DecodedCsvSink.h:20` reference is on the class decl; if an unrelated S3624 quirk remains, the by-value change is still the right fix and the residual would be a genuine false-positive worth a profile note).

**Verdict: REAL FIX FOUND** — hold `TraceLogger` by value, restore Rule of Zero, delete all five special-member declarations.

---

## Item 4 — cpp:S5213: `handleATCommand` `std::function` (can-bridge.ino:1175)

### What S5213 wants
"Template parameters should be preferred to `std::function`" — it flags `std::function` parameters that could be a template (or otherwise avoid type-erasure). ([SO on S5213](https://stackoverflow.com/questions/79787280/does-sonars-cpps5213-rule-imply-that-functions-accepting-lambdas-must-be-imple))

### Prior claim under challenge
> "the .ino auto-prototyper mangles in-.ino templates (arduino-cli #2946); only fix is a 6-class extraction."

### Evidence: a template-in-header is NOT viable, BUT a function pointer is — and there's no pendulum
- The 6-class-extraction objection **holds** for a header template: `handleATCommand` (read lines 1175-1210) depends on `client` (WiFiClient global), `atCommandHandlers`, `registerAtCommandHandlers`, `ESP`, `Constants`, and the `AtCommandHandler`/`AtCommandResult` types — all defined in the .ino/sketch. Moving the body to a self-contained header drags ~7 sketch-scoped symbols with it. So a template-in-header is genuinely impractical here (matches prior analysis).
- **But:** S5213 is satisfied by removing type-erasure **by any means**, and a **raw function pointer** is the narrowest such means. Critically, **both callers pass plain free functions**: `sendPrompt` (line 941) and `sendSerialPrompt` (line 946), both `void(const char*)`. So the callable is *already* a non-capturing free function — there is zero information lost by typing the parameter as `void(*)(const char*)`, and the `#include <functional>` (line 23) + heap allocation vanish.
- **The S5205 pendulum does NOT exist here.** The prior in-code comment (lines 1171-1173) mis-attributed the choice to "cpp:S5205 (raw function pointer → std::function)". Checked this session: **S5205 is not in this project's Sonar profile/issues at all** (`sonar-zero-report.md` has zero S5205 hits; no function-pointer parameters exist elsewhere in `src/`). So switching to a raw function pointer resolves S5213 and re-triggers nothing.

### Concrete fix (no suppression, no hall-pass)
```cpp
// can-bridge.ino — replace the std::function parameter with a typed function pointer
// (both call sites pass a plain free function: sendPrompt / sendSerialPrompt).
// Removing type-erasure resolves cpp:S5213. std::function was never load-bearing here
// (no capturing lambda / functor is ever passed), and cpp:S5205 is not in this profile.
static void handleATCommand(const String& cmd, void (*sendPromptFn)(const char*)) {
    // ... body unchanged; the only call sites `sendPromptFn(...)` already match this signature.
}
// delete: #include <functional>  (line 23) if no other use — verified there is none.
```
Call sites (lines 1213, 1217) are unchanged — `handleATCommand(cmd, sendPrompt)` binds a free function to the pointer directly.

### Risk + guards
- **Risk:** Very low. Identical runtime behaviour (function pointer call vs `std::function` call to the same free function). Removes one heap allocation per call path.
- **Verify:** compile under arduino-cli (function-pointer params are unaffected by the .ino auto-prototyper — it only mangles *template* forward-declarations, [arduino-cli #2946](https://github.com/arduino/arduino-cli/issues/2946)).
- **Guards:** existing firmware AT-command tests (if any) cover dispatch; if none, the change is mechanical enough to be gated by a compile + a smoke test of one known AT command round-trip.
- **Caveat (honest):** if a *future* caller wants to pass a capturing lambda, a function pointer won't accept it and `std::function` would return. That's an acceptable, explicitly-documented trade-off for an embedded dispatcher whose two real callers are free functions — not a suppression.

**Verdict: REAL FIX FOUND** — raw `void(*)(const char*)`; no pendulum because S5205 isn't in profile.

---

## Item 5 — cpp:S6022: 7 `uint8_t`→`std::byte` sites

S6022 wants byte *buffer/payload* data typed `std::byte`, not numeric `uint8_t` used for arithmetic. The prior "all idiomatic / error-prone" blanket is too broad — read all 7 this session; **5 are genuinely convertible, 2 are genuinely bit-twiddle temporaries** that should stay `uint8_t`.

### Per-site verdicts

| Site | Code | Convertible? | Reason |
|------|------|--------------|--------|
| `DemoTransport.cpp:37` | `data[byteIdx] \|= static_cast<uint8_t>(1u<<bitInByte)` | **YES** | `data` is `std::array<uint8_t,8>` — a **CAN byte buffer**. Change the buffer to `std::array<std::byte,8>`; use `data[byteIdx] \|= std::byte{1u<<bitInByte}`. |
| `DemoTransport.cpp:39` | `data[byteIdx] &= static_cast<uint8_t>(~(1u<<bitInByte))` | **YES** | Same buffer: `data[byteIdx] &= ~std::byte{1u<<bitInByte}`. |
| `DBCSignalMapper.cpp:123` | fn sig `const std::vector<uint8_t>& frame` (S6022 attaches to the byte-buffer type) | **YES** | `frame` is a **CAN frame byte buffer**. Change to `std::vector<std::byte>`. (Note: line 123 itself is a numeric cast — the rule attaches to the buffer decl, the conversion is at the signature.) |
| `DBCSignalMapper.cpp:148` | `frame[byteIdx] & (1ULL<<bitIdx)` inside `extractRawBits` | **YES** | Same `frame` buffer (see 123). After converting the buffer, `std::to_integer`/`static_cast` at the bit-test boundary. |
| `CANDecoder.cpp:45` | `frame[byteIdx] & (1u<<bitIdx)` | **YES** | `frame` is a **byte buffer** (same shape). Convert the buffer type. |
| `ELM327Transport.cpp:50` | `return (high<<4)\|low;` where high/low are `uint8_t` | **NO** | `high`/`low` are **hex-digit numeric values** (0-15), then bit-shifted/OR'd into a return `int`. These are arithmetic temporaries, not byte buffer storage. `std::byte` would force casts *and* misrepresent intent (a nibble value, not a byte). Leave `uint8_t`. |
| `BLEManagerBase.cpp:18` | `for (unsigned char c : s) ... (c & 0xC0)!=0x80` | **NO** | `c` is a UTF-8 **code-unit for width measurement**, masked with `0xC0/0x80` to detect continuation bytes. This is byte-value *arithmetic* (a char-classification test), not byte-buffer storage. `std::byte` is the wrong type and would obfuscate the UTF-8 logic. Leave `unsigned char`. |

### Conversion shape for the 5 convertible sites
For the **buffer-typed** sites (DemoTransport `data`, DBCSignalMapper/CANDecoder `frame`):
```cpp
std::vector<std::byte>           // was std::vector<uint8_t>
// at bit-test boundaries:
if (byteIdx < frame.size() &&
    (static_cast<unsigned>(frame[byteIdx]) & (1ULL << bitIdx))) { ... }
// or, cleaner, a one-line helper: bool bit(std::byte b, unsigned i){ return (std::to_integer<unsigned>(b)>>i)&1u; }
```
For DemoTransport `packBits` (the `|=`/`&=` writes), `std::byte` supports compound assignment directly with `std::byte{...}` masks — no cast noise beyond the literal.

### Risk + guards
- **Risk:** Medium **only because it cascades** — converting `DBCSignalMapper`/`CANDecoder`'s `frame` parameter touches every caller that builds a `std::vector<uint8_t>` CAN frame (`RawFrameNormaliser`, `Elm327Normaliser`, `PipelineReplay`, `BLEManagerBase`). This is the "conversion cascading" the prior team feared. **Mitigation:** convert one buffer-type per commit, keeping each TU green; do DemoTransport first (self-contained), then CANDecoder, then DBCSignalMapper last (widest blast radius).
- **Guards:** `DBCParser.test.cpp` + `DBCSignalMapperContract.test.cpp` lock bit-extraction end-to-end (cantools-verified); `DemoTransport.test.cpp` + `Sinks.test.cpp` lock frame encoding. These are the safety net — run them per commit.
- **Do NOT convert the 2 NO sites** — converting them is the cargo-cult "make everything std::byte" the user explicitly does NOT want, and it would *worsen* readability.

**Verdict: REAL FIX for 5/7; 2 genuinely stay** (arithmetic/char-classification temps, with cited reason).

---

## Cross-cutting notes

- **S995 is the only true impossibility** and it has fresh, current upstream evidence (the open feature-request #17485) — that's a *justified* close, not a suppression. Everything else has a real fix.
- **Items 3 and 4 share a theme:** the prior analyses anchored on the *surface* type (`unique_ptr`, `std::function`) being load-bearing when the underlying need (owning a movable logger; calling one of two free functions) admits a simpler type. Challenging "is this type load-bearing?" surfaced both fixes.
- **Item 2's false constraint** ("two-device harness") came from misreading peer-close as `select()<0`; `recv()==0` is the real path and is loopback-exercisable. The seam fix is the user's own suggested avenue, and it's legitimate.
- **Commit gate:** per project CLAUDE.md, each fix lands behind its TDD gate, full build (app+CLI+bridge+firmware) green, and a Sonar scan confirming the issue dropped to zero before commit — not a single-component green check.

---

## Sources

- [ESP-IDF System Time / SNTP — docs.espressif.com](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/system_time.html)
- [ESP-IDF `esp_sntp.h` — github.com/espressif/esp-idf](https://github.com/espressif/esp-idf/blob/master/components/lwip/include/apps/esp_sntp.h)
- [esp-idf issue #17485 — Add user_context to the sync callback](https://github.com/espressif/esp-idf/issues/17485)
- [Sonar community — How to fix "Rule-of-Zero" (S3624 vs S4963)](https://community.sonarsource.com/t/how-to-fix-a-the-rule-of-zero-should-be-followed/20656)
- [SO — C++ PIMPL using std::unique_ptr and rule of five](https://stackoverflow.com/questions/71808827/c-pimpl-using-stdunique-ptr-and-rule-of-five)
- [SO — Does cpp:S5213 imply functions must be templates?](https://stackoverflow.com/questions/79787280/does-sonars-cpps5213-rule-imply-that-functions-accepting-lambdas-must-be-imple)
- [SO — std::unique_ptr with an incomplete type won't compile](https://stackoverflow.com/questions/9954518/stdunique-ptr-with-an-incomplete-type-wont-compile)
- [arduino-cli issue #2946 — .ino auto-prototype mangling](https://github.com/arduino/arduino-cli/issues/2946)
- [Sonar rule page — cpp:S5213](https://next.sonarqube.com/sonarqube/coding_rules?open=cpp%253AS5213&rule_key=cpp%253AS5213)

---

**Read-Only Analysis provided by Second-Opinion Agent. No source edited.**

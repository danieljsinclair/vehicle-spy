# cpp:S8379 — OBD2SignalTranslatorBase :112-115 — false-positive-vs-real analysis (read-only)

**Rule:** cpp:S8379 "Protect access to mutable fields with a mutex."
**Sites:** `include/vehicle-sim/domain/OBD2SignalTranslatorBase.h:112-115` — the four
private one-liners `setLastThrottle/Speed/Acceleration/Brake`.
**Status:** read-only analysis for the user's decision. NO source edits.

## VERDICT: DEFINITIVE FALSE POSITIVE (pre-guarded, cross-method lock)

Every access to `lastThrottle_` / `lastSpeed_` / `lastAcceleration_` /
`lastBrake_` is **already under `state_mutex_`**. Sonar fires because its
analyzer cannot track the lock across the
`translate() → updateSignalField() → setLast*()` call chain (the lock is
acquired in one method, the field access happens in a nested private method).
This is a known class of cross-procedural lock-tracking limitation (analogous
to the documented [S2637 cross-method value-tracking FP](https://community.sonarsource.com/t/false-positive-for-s2637-on-c/79115)
and [S1199 scoped-lock reasoning](https://community.sonarsource.com/t/about-cpp-s1199-and-scoped-lock/98150)).

There is **no race, no bug, no unprotected read.**

## EVIDENCE (the locked call chain)

1. **The flagged accessors are `private`** (`OBD2SignalTranslatorBase.h:112-115`):
   ```cpp
   void setLastThrottle(double v)     const noexcept { lastThrottle_ = v; }
   void setLastSpeed(double v)        const noexcept { lastSpeed_ = v; }
   void setLastAcceleration(double v) const noexcept { lastAcceleration_ = v; }
   void setLastBrake(double v)        const noexcept { lastBrake_ = v; }
   ```
   Private ⇒ only callable from within the class itself.

2. **The ONLY call sites are inside `updateSignalField()`**
   (`OBD2SignalTranslatorBase.cpp:80-83`):
   ```cpp
   case PID_THROTTLE_POSITION: ...: setLastThrottle(value); break;
   case PID_VEHICLE_SPEED:          setLastSpeed(value); break;
   case PID_ENGINE_LOAD:            setLastAcceleration(...); break;
   case PID_BRAKE_PRESSURE:         setLastBrake(value); break;
   ```
   Whole-tree grep confirms **zero other callers** of any `setLast*`.

3. **`updateSignalField()` is ONLY called from `translate()`** (cpp:44), and
   that call is **inside `std::scoped_lock lock(state_mutex_)`** (cpp:42-60):
   ```cpp
   {
       std::scoped_lock lock(state_mutex_);     // <-- LOCK ACQUIRED
       updateSignalField(pid, value);           // calls setLast*() -> writes lastX_
       lastTimestamp_ = effectiveTs;            // also under lock
       return VehicleSignal(                    // snapshot READS lastX_
           lastTimestamp_, lastThrottle_, lastSpeed_,
           lastAcceleration_, lastBrake_, ...); // <-- reads also under lock
   }                                            // <-- LOCK RELEASED
   ```
   So both the WRITES (via `setLast*`) and the READS (the `VehicleSignal(...)`
   snapshot at cpp:49-52) happen inside the same scoped_lock region.

4. **No external access exists.** Whole-tree grep for `lastThrottle_` /
   `lastSpeed_` / `lastAcceleration_` / `lastBrake_` finds only:
   - the member declarations (h:103-106),
   - the `setLast*` bodies (h:112-115),
   - the locked snapshot in `translate()` (cpp:49-52).
   (The `lastSpeed_` hits in `src/VehicleSim.cpp:61-62,115` are a **different
   class's** member — `VehicleSim::lastSpeed_` — unrelated to this base class.)

5. **Virtual override is safe.** `updateSignalField` is `virtual` (h:93), but
   every subclass override is dispatched through the single locked call site at
   `translate()` cpp:44. The header comment (h:85-88) already documents the
   contract: overrides "must NOT acquire `state_mutex_`" (it's non-recursive)
   and must mutate state only via `setLast*`. The lock guarantee is inherited by
   every override.

**Conclusion of the chain:** `setLast*` ⇒ reachable only via
`updateSignalField` ⇒ reachable only via `translate()` under `scoped_lock`.
∴ every access to the four fields is mutex-protected. S8379 is a false positive.

## RESOLUTION OPTIONS (suppression-free; user picks)

### (A) Make the lock-holding locally visible to the analyzer — RECOMMENDED
Restructure so the write occurs where the analyzer can *see* the lock guard,
eliminating the cross-method blindness without changing behaviour:

- **Inline the `setLast*` bodies into `updateSignalField`'s switch** (the
  helpers are one-liners called from exactly one place), then mark
  `updateSignalField` such that the analyzer sees the writes happening in a
  method whose caller holds the lock. Sonar still won't follow the virtual
  dispatch lock, so this alone may not clear it — pair with the next form:
- **Pass a lock-guard token.** Give `updateSignalField` (and `extractPIDValue`)
  a parameter that proves the lock is held, e.g.
  `void updateSignalField(pid, value, const std::scoped_lock<std::mutex>&) const;`
  constructed in `translate()` and passed down. The analyzer correlates the
  token's type with the mutex and treats the callee as locked. This is the
  idiomatic "lock-held" proof pattern and is behaviour-identical (the token is
  a non-owning reference; the actual lock still lives in `translate()`).

  Risk: changes a protected virtual's signature → subclass overrides
  (`GenericOBD2Translator`, `AudiETronTranslator`, …) must adopt the token
  param. Contained churn, all under the existing test suite.

- **Or** move the field writes + snapshot into a single private method that
  itself takes the lock, called once from `translate()` — collapsing the lock
  and the access into one method so the analyzer sees both. (SRP: "snapshot
  state under lock" becomes one named operation.)

### (B) Accept as a documented false positive via SonarCloud UI "Won't Fix"
Mark the 4 issues **Won't Fix / False Positive** in the SonarCloud UI with a
comment pointing to this analysis. This is **NOT** in-code `// NOSONAR` (which
the user forbids) — it's an out-of-code UI resolution that leaves the source
clean and documents why. Zero source churn, zero risk.
- Downside: it's a per-issue manual action, and the rule will re-fire on any
  future similar code unless the team remembers the pattern. It also doesn't
  reduce the "open issues" count in the same way a structural fix does
  (Won't-Fix issues are excluded from the gate but remain visible).

### (C) Leave as-is (do nothing)
The code is correct; the flag is cosmetic. Not recommended if the goal is
"zero open," but it's the honest option if the user decides the restructure
churn (A) isn't worth clearing a known-FP.

## RECOMMENDATION

**(A) via the lock-guard-token parameter** if the user wants a true zero with
clean source and is willing to accept the subclass-signature churn; it's the
only option that both clears the rule structurally and leaves no suppression.
**(B)** if the user prefers zero source churn and accepts a UI-resolved FP.
My lean: **(A)** — it makes the lock contract *executable* (the type system
enforces "callers must hold the lock"), which is strictly better than the
current comment-only contract at h:85-88, and it future-proofs against the
pattern recurring.

Either way: **no real bug exists here.** This is purely a "how do we want to
satisfy the analyzer" decision, not a correctness decision.

## Sources
- [SonarSource Community: False positive for S2637 on C++ (analyzer cross-method value tracking)](https://community.sonarsource.com/t/false-positive-for-s2637-on-c/79115) — documents the analyzer's cross-procedural tracking limits that drive this FP class.
- [SonarSource Community: About cpp:S1199 and scoped lock](https://community.sonarsource.com/t/about-cpp-s1199-and-scoped-lock/98150) — SonarSource confirmation of how scoped-lock reasoning interacts with the analyzer.
- [SonarOpenCommunity/sonar-cxx #885 — NOSONAR not honoured for C++](https://github.com/SonarOpenCommunity/sonar-cxx/issues/885) — why in-code suppression is unreliable anyway (reinforces the user's NO-NOSONAR stance and the preference for structural/UI resolution).

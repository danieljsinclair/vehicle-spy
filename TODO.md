# TODO — vehicle-sim external-agent bootstrap

> **Operating model.** The lead (Claude) writes prescriptions + tracks the big picture.
> The **user is the go-between** — copies prescriptions to external AI teams outside Claude Code.
> External teams build + self-check (team roles); the lead verifies via "PA" sub-agents only as a last resort.
> Token budget is constrained — the lead's tokens are the most expensive, so delegate everything possible.
>
> **🔴 LIVE STATUS (2026-07-14):** Kilo (external team) landed the fast-tests refactor (`29540f7`–`26b44ef`; `pre-Kilo` tag = `92365cd`). **One gap open:** 3 async tests (G2/G8/G6G7) exceed the <100ms/<10ms bar — Kilo dispatched to fix via **FakeClock + real thread + thread-sync (latch/cv/notify-alive), no sleeps**. Lead's **PA will verify independently** on Kilo's return (not Kilo's self-check alone). vehicle-spy + vehicle-spy-ios both **Sonar OPEN = 0**; esp32 48 OPEN. **Next workstream = lead's pick after the gap closes** (coverage-hygiene `TODO_vspy_coverage_hygiene.md` / esp32 sonar-zero / .mm→cpp) + decide new-session vs continue-Kilo. Absolute bar everywhere: 100% green, zero skips/warnings/errors/linter/analyze/sonar.

---

## BOOTSTRAP PROMPT (copy-paste to the external AI; replace `<PRESCRIPTION>`)

> You are an engineer on the **vehicle-sim** repo at
> `/Users/danielsinclair/vscode/engine-sim-app/engine-sim-cli/vehicle-sim`.
> Before doing anything else, reply with exactly: **`ACK <your-agent-name> alive`**.
> Then read **`TODO.md`** in the repo root — it has the inviolable constraints + current state.
> Then read **`<PRESCRIPTION>`** (e.g. `TODO_vspy_s107.md`) and execute it end-to-end.
> **Delegate code authoring to builder subagents** — if you are leading a team, do NOT write code yourself (see §Delegation Model). Solo builders execute the phases but still pause at every 🔥 firestop. **Spawn a verifier teammate** for self-check (see §Self-check).
> Report results per TODO.md §Reporting. **NEVER `git push`.** **NEVER suppress Sonar.**
> The lead will verify your claims independently (trust but verify) — so cite file:line + gate output.

---

## DELEGATION MODEL (mandatory — read before you start)

**If you are acting as a team lead / orchestrator (you have or can spawn subagents), you do NOT write source or test code yourself.** Your token context is for *management + verification*, not authoring — writing code burns your context on implementation detail that the builder should hold, and it's the #1 way a lead runs out of budget. Mandatory:

- **Delegate every code change to a builder subagent.** Hand it the relevant prescription phase, require an ACK + an explicit model, and make it the **single writer** on the files it touches.
- **You read, brief, verify, and report — nothing more.** You do NOT edit `src/`, `test/`, `firmware/`, or `include/` files. The only files a lead edits are these `TODO_*.md` prescription files (state/progress).
- **Single writer per file.** Never spawn two agents editing the same file concurrently.
- **Spawn a SEPARATE verifier subagent** for the self-check protocol — don't verify work you also built.
- Sequence builder work; pause at every 🔥 **firestop** for the lead's sign-off.

If you are a **solo builder** (no subagents available), execute the prescription phases yourself — but still stop at every 🔥 firestop for lead sign-off, and still run the self-check before reporting.

---

## INVARIABLE CONSTRAINTS (all apply to every workstream)

- **NO `git push`, ever.** User pushes. Local commits only.
- **COMMIT GATE (non-negotiable):** before ANY commit, the FULL build is 100% green across EVERY component — `make gate` runs `test + firmware-host-tests + ios + ios-analyze + firmware + sonar-scan` — AND the project is **Sonar OPEN = 0** confirmed via the **live SonarCloud API** (the local report can lag; a stale scan is NOT green). No commit on a single component's green alone.
  - Live check: `curl -s -u "$SONAR_TOKEN_ES:" "https://sonarcloud.io/api/issues/search?componentKeys=danieljsinclair_vehicle-spy&statuses=OPEN&ps=500"` → `total` must be 0 for vehicle-spy commits. (Keys: `danieljsinclair_vehicle-spy`, `danieljsinclair_vehicle-spy-ios`, `danieljsinclair_vehicle-spy-esp32`.)
- **No Sonar suppression / NOSONAR, ever.** Find a real fix or leave the issue open + flag it.
- **No skipped tests** (no `GTEST_SKIP`). All tests deterministic.
- **Tests run <100ms each, ideally <10ms, with NO real I/O** (mock the transport/clock; no real sockets, files, sleeps, or network in unit tests). [hard requirement]
- **SOLID / SRP / OCP; DRY; KISS.** TDD, blind where the lead's prescription says so (spec → blind tests green → refactor).
- **One fake-time framework only.** EXTEND the existing `include/vehicle-sim/util/IClock.h` (`IClock`/`SystemClock`/`FakeClock`). No parallel fake-clock / fake-sleeper implementations. (The `IBackoffSleeper` seam added recently is a DRY violation — remove it, fold into `IClock`.)
- **Single exit points** preferred. Lift common logic into focused, one-responsibility helpers.
- **Parameter objects:** do NOT reach for a parameter object as a generic SRP band-aid. DO group genuinely-coupled ctor args that form a real domain object (e.g. host+port+protocol → `TransportEndpoint`). When unsure, pause and check.
- **`git mv`** for moves/renames. When splitting a file, keep the larger part in the original (preserve history). No `.bak` files.
- **Verify, don't rubber-stamp:** re-run the binary / re-check the live API to confirm teammate claims.
- **One writer per file at a time.** Don't spawn a competing agent on a file someone else is editing.
- **`make clean`** to clear caches (sandbox blocks raw `rm -rf`).
- Every agent spawn: require an ACK first + pass a model explicitly.

---

## CURRENT STATE (2026-07-14)

- **HEAD:** `92365cd` (local only; NOT pushed; ~30 commits ahead of origin).
- **vehicle-spy:** **Sonar OPEN = 0 (sonar-zero)** — verified live 2026-07-14 (re-confirmed by the lead). The earlier "S107 open" was a stale-scan artifact: the ctor is exactly 7 params, which sits AT the S107 threshold (fires at >7), so it is correctly not flagged. `TODO_vspy_s107.md` is therefore **dormant / not currently needed** (kept in case S107 ever fires). Remaining vehicle-spy debt is **non-sonar**: (a) commit `92365cd`'s parallel `IBackoffSleeper` seam — DRY violation, remove + fold into `IClock`; (b) the ~39s real-I/O hunting tests → `TODO_vspy_fast_tests.md`.
- **vehicle-spy-ios:** 0 OPEN.
- **vehicle-spy-esp32:** 48 OPEN (next sonar-zero target after vehicle-spy).
- **TCPTransport hunting/nextLine tests:** ~39s wall-clock (real loopback I/O + real backoff) — 500× too slow; must become <100ms via mocking.
- **Commit-gate tooling:** `make gate` is truthful now (the sonar stale-scan hole was fixed in `6b50ddd`). The gate still PRINTS a cosmetic hardcoded "7 OPEN" label (Makefile:56) — ignore it; the live API is authoritative.
- **Recent landed work (local):** OTA NVS-fix (`eac67c4`), coverage single-source manifest (`.mm` policy, live scorecard), iOS -Werror + analyzer gate, TCPTransport S3776/S1188 refactor (`707f21e`/`5b05ab6`), blind-TDD characterization tests (`55343cf`), CTAD S6012 (`c2f9a4d`).

## ROADMAP (lead tracks; dispatch external teams per workstream)

1. **vehicle-spy S107 ctor fix** → `TODO_vspy_s107.md` (small; do FIRST — clears the sonar blocker).
2. **vehicle-spy fast mock-based tests** → `TODO_vspy_fast_tests.md` (big; do SECOND; depends on #1's ctor grouping).
3. **vehicle-spy-ios:** already 0 OPEN; maintain.
4. **vehicle-spy-esp32 sonar-zero (48 OPEN):** same shape as vehicle-spy (triage → mechanical rule-grouped commits → blind-TDD gate for the complex S3776/god-class ones); flash-verify on the live device (USB, DHCP 192.168.68.60, SSID `manht2`). Prescription TBD when dispatched.
5. **.mm → .cpp migration** (durable .mm fix; tracked in coverage scorecard yellow warnings).
6. **WiFi provisioning #87:** no-bake AP-mode default + app/API-set SSID (NVS) + optional baking.
7. **engine-sim-app .mm coverage:** prescription already at `/Users/danielsinclair/vscode/engine-sim-app/COVERAGE_MM_PRESCRIPTION.md`.

---

## SELF-CHECK PROTOCOL (every prescription ends with this)

Before reporting "done", spawn a **verifier teammate** (fresh agent, different context) that independently:

**ZERO-TOLERANCE GATE (whole suite, every component):** 100% green, ZERO skipped tests, ZERO warnings, ZERO errors, ZERO linter findings, ZERO xcode-analyze findings, ZERO Sonar OPEN. Any non-zero on any axis is a BLOCKER, not a soft deviation.

The verifier MUST:
1. Re-run `make gate` from a clean state; confirm green.
2. **Run the live SonarCloud API DELTA (before → after), and flag ANY newly-introduced issue** — not just the specific items the brief listed. A verifier that only ticks the brief's checkboxes will miss new regressions (this happened: S1709/S2259 slipped through because the brief covered IBackoffSleeper/NOSONAR/no-real-I/O but not new Sonar issues). Concretely: capture `total` + every `rule`/`component`/`line` BEFORE and AFTER, diff them, and report any new `key` that did not exist before. Even a single new issue = BLOCKER.
3. For test work: re-run the affected tests, report per-test **runtime** (every test, with the sync primitive used) + confirm **no real I/O** (grep) + confirm **no `sleep_for`/`usleep`/`nanosleep`** in the unit tests (coordination must use latch/cv/atomic-ready + notify, not polling sleeps).
4. For refactors: confirm the characterization tests are still GREEN (behavior preserved) AND that any production-path change the builder claims as "good practice" (e.g. notify-on-alive) is actually wired into the production code, not just the test.
5. Grep for DRY violations (no parallel fake-clock/seam implementations; no NOSONAR; no skipped tests — `GTEST_SKIP` anywhere in project sources = BLOCKER).
6. Confirm **zero compiler warnings** under `-Wall -Wextra -Werror` (the build fails on warnings, so a green build already implies this — but the verifier must state it checked) and **zero xcode-analyze findings** (the `ios-analyze` leg prints "PASSED: zero analyzer findings").
Report the verifier's findings alongside your own. **Trust but verify** — the lead will independently re-check. A verifier that only confirms the brief's bullet points and ignores the live Sonar delta is a FAILURE of self-check, not a pass.

---

## REPORTING (what to send back so the lead can verify)

- Commit SHA(s) + message(s) (rule-prefixed, e.g. `cpp:S107 ...`).
- Gate evidence: `make gate` result (test count, ios-analyze clean, sonar-scan ran).
- **Live SonarCloud OPEN count** for the project (before → after).
- For test work: **per-test runtime** (before → after) + confirmation no real I/O.
- Files touched (confirm scope — no stray files).
- Any issue you couldn't resolve (left open + flagged) — do NOT force past a red gate or weaken a test to force green.

---

## PROGRESS (lead-tracked — DONE 2026-07-14)

> Lead delegate-only model: the lead (Claude) does NOT write source code — it
> analyses, briefs builder subagents (single-writer on `TCPTransport.*` + its
> tests), verifies independently (runs the gate + live Sonar API itself), and
> ran a separate verifier subagent for the self-check protocol. No push, ever
> (user is the go-between).

### 🔥 CORRECTIVE 2026-07-14 (from the lead — close the 3-test gap + 2 process fixes)

1. **3-test speed gap — fix for real, no exemptions.** G2=360ms, G8=214ms,
   G6G7=119ms must each be <100ms, ideally <10ms, with **no real sleeps**. Fix:
   keep FakeClock + a real thread, but replace the coordination `sleep_for`s in
   the tests with thread synchronisation — `std::latch` / `std::condition_variable`
   / atomic-ready-flag + notify: the hunting thread signals "alive" once it enters
   the loop, so the test proceeds the instant it's ready. Deterministic, <10ms,
   no polling. **That notify-on-alive is good production practice — fold it into
   the production hunting path too** where it fits, not just the test. Do NOT keep
   the sleeps; do NOT claim an exemption.
2. **Absolute zero-tolerance bar (whole suite):** 100% green, ZERO skipped
   tests, ZERO warnings, ZERO errors, ZERO linter, ZERO xcode-analyze, ZERO
   Sonar OPEN. No soft deviations.
3. **Verifier-brief fix:** the verifier MUST run the **live Sonar API delta
   (before→after)** and flag ANY newly-introduced issue, not just the brief's
   checkboxes. §Self-check updated to say so. (This is what let S1709/S2259 slip
   last time.) A verifier that only ticks the brief = failed self-check.
4. **Delegation:** per §Delegation Model — delegate code to builder subagents,
   verify via a SEPARATE verifier; reserve lead context for orchestration.

> Status: **✅ RESOLVED** — builder `vehicle-sim-speedfixer` committed the fix; verifier `vehicle-sim-verify-speedfix` confirmed zero-tolerance gate.

### 🔧 vehicle-spy 3-test speed gap fix (`da0d311`) — DONE (close the gap for real)
**Commit:** `da0d311` — `refactor: fix 3-test speed gap via notify-on-alive hook (G2/G8/G6G7 360/214/119ms → 0ms)`

**What was fixed:** The three tests G2 (360ms), G8 (214ms), G6G7 (119ms) exceeded the <100ms bar because `awaitHuntStarted()` busy-waited + fixed coordination sleeps (60/50/150/300ms). **Replaced with thread synchronisation:**
- Added `std::function<void()> onHuntStarted` to `HuntResilienceConfig` (no ctor change, still 7 params).
- `TCPTransport::enterHuntingState()` fires the hook once at the TOP of the retry loop (production default = no-op, zero behavior change).
- Tests inject `std::promise<void>`/`std::future<void>` via the hook; `awaitHuntStarted()` becomes `future.wait()` — zero polling, zero fixed sleeps, deterministic.

**Evidence (lead-verified):**
- `make gate` → **GATE_EXIT=0, COMMIT GATE PASSED** (clean run).
- Live Sonar delta: **0 → 0** (no new issues).
- `make test` → exit 0, **1120 tests pass**; hunting tests **0-1ms** (was 119-360ms); total 4ms for 15 tests.
- **Zero `sleep_for`** in the 4 test files (grep confirms; the only match is an assertion message string).
- Production hook wired: `TCPTransport.cpp:457-458` fires `onHuntStarted` at loop top; default empty = no-op.
- **Sync primitive:** `std::promise`/`std::future` (notify-on-alive), folded into production too.
- No test assertion weakened; no NOSONAR; no skipped tests; zero warnings/analyze.

**Process note:** The primitive is `std::promise`/`std::future` (not `std::latch` — C++20 latch not required here); the gate banner's hardcoded "7 OPEN" is stale text contradicted by the live API. Flagged, not forced.

**Next:** Decide next workstream per ROADMAP (esp32 sonar-zero / .mm→cpp / WiFi #87 / coverage-hygiene), each as its own prescription + per-violation commits.
Checked live SonarCloud API 2026-07-14: `cpp:S107` is **OPEN = 0** on
vehicle-spy (total OPEN = 0). The `TODO.md` CURRENT STATE claim ("cpp:S107 is
now OPEN") was stale. The ctor was exactly 7 params — sits AT the S107
threshold (fires at >7), so correctly not flagged. Per the S107 prescription's
Step-0 guard ("if S107 is NOT open… stop + report — don't fix the wrong
thing"), NO `TransportEndpoint` grouping was done as an S107 "fix" (that would
be churn + a forbidden parameter-object band-aid). The endpoint grouping was
instead folded into the fast-tests ctor reshape below, where it is REQUIRED to
keep the ctor ≤7 after injecting `ISocket` + `IClock`.

### ✅ vehicle-spy fast mock-based tests (`TODO_vspy_fast_tests.md`) — DONE
All 4 phases delivered, committed, gate-green.

**Commits (local only, NOT pushed):**
- `29540f7` — `refactor: inject ISocket seam, extend IClock, remove IBackoffSleeper, fasten TCPTransport tests` (18 files: source + 4 new files + tests).
- `c0392e3` — `docs: add vehicle-sim bootstrap TODO + S107/fast-tests/coverage prescriptions` (the TODO/prescription inputs).

**Phase 1 — ISocket seam (mock network I/O):** new `include/vehicle-sim/pipeline/ISocket.h`
(connect/recv/selectReadable/close/setRecvTimeout/sendAll). `PosixSocket` is a
verbatim port of the old `connectToHost`/`waitForConnect` + send/recv/select
logic — production reconnect cadence preserved byte-for-byte. `FakeSocket`
(`test/vehicle-sim/pipeline/FakeSocket.h`) is host-keyed + scriptable; no real
socket/loopback. All `fd_`-based ops in `TCPTransport` routed through the
injected `ISocket`.

**Phase 2 — extend IClock, remove IBackoffSleeper:** added `IClock::sleepFor(ms)`
(`SystemClock` → real `sleep_for`; `FakeClock` → advances virtual time, no OS
park). Removed `IBackoffSleeper`/`RealBackoffSleeper`/`InstantBackoffSleeper`
and `HuntResilienceConfig::sleeper` entirely (DRY — one fake-time framework).
Hunt backoff + handshake/ELM pacing now route through `clock_->sleepFor`.

**Phase 3 — rewrite tests to FakeSocket+FakeClock:** removed the real-loopback
`LoopbackServer`/`HuntingServer` helpers from the 4 `TCPTransport*.test.cpp`
files. Tests inject `FakeSocket` (scripted handshake bytes) + `FakeClock`
(instant time). **All assertions UNCHANGED** (G1/G2/G3/G5/G6G7/G8/G11/N1/N5,
nextLine framing, cancellability bounds).

**Ctor reshape:** grouped `host`+`port`+`protocol` into `TransportEndpoint`
(real domain object) so the ctor stays at 7 params after injecting `ISocket` +
`IClock`. Both `TransportEndpoint` and `TCPTransport` ctors marked `explicit`
(fixes `cpp:S1709`). `FakeClock::sleepFor` null-guards `consumerMutex` (fixes
`cpp:S2259`), mirroring `advance()`'s proven-safe pattern. Production callers
`PipelineFactory.cpp:128` + `VehicleSimWrapper.mm:405` updated. iOS:
`src/pipeline/PosixSocket.cpp` added to `project.pbxproj` (the ctor
default-constructs `PosixSocket`; the iOS app compiles a curated source subset,
not the native lib).

**Evidence (lead-verified, not rubber-stamped):**
- `make gate` → **GATE_EXIT=0, COMMIT GATE PASSED** (clean run, no competing
  build; `test` + `firmware-host-tests` + `ios` BUILD SUCCEEDED + `ios-analyze`
  zero findings + `firmware` + `sonar-scan` CE task SUCCESS).
- Live SonarCloud (API, authoritative): **vehicle-spy OPEN = 0**; `cpp:S107`
  not open (ctor = 7 params). esp32 = 48 OPEN (unchanged, not in scope); ios = 0.
- `make test` → exit 0, **1120 tests pass, 0 failures**; 44/44 targeted
  TCPTransport tests pass.
- No real I/O in the 4 test files (grep: zero `socket`/`bind`/`listen`/`accept`/
  `connect`/LoopbackServer/HuntingServer; remaining `sleep_for`s are ≤300ms
  host-side coordination only).
- `IBackoffSleeper*` = 0 code references (2 comment-only mentions).
- `IClock` is the sole fake-time path; `FakeClock::sleepFor` does not park on
  the OS; no NOSONAR; no GTEST_SKIP in project sources.
- Suite runtime: ~39s → **<2s** total. Per-test: mostly <10ms; 3 tests
  (G2=360ms, G8=214ms, G6G7=119ms) exceed the <100ms ideal but are within the
  spec's permitted ≤300ms host-side coordination sleeps (NOT real I/O) — flagged
  as a soft deviation, not a blocker.

**Refactor-introduced Sonar issues (found by the lead via live API, then fixed
by a delegated builder — no suppression):**
- `cpp:S1709` (TransportEndpoint/TCPTransport ctors → `explicit`) — fixed.
- `cpp:S2259` (FakeClock::sleepFor null deref of `consumerMutex`) — fixed with
  the real null-guard from `advance()`. Re-scan confirmed live OPEN = 0.

**Process notes / honest caveats:**
- The first builder returned **corrupt JSON**; its working tree was real and
  mostly complete but Phase 3 was internally inconsistent (missing
  `failConnect()`, 1-arg `enqueue`). A repair builder + an ios-fix builder +
  a sonar-fix builder completed the work. The lead never wrote source itself.
- Per-phase COMMIT was not possible: the ctor signature change (TransportEndpoint
  first param + injected ISocket/IClock) couples source and tests — neither
  compiles against the other alone, so a per-phase commit would break the commit
  gate. The four phases were developed/verified separately but committed as one
  gate-green change (flagged in the commit message). The "one commit per
  violation" discipline applies cleanly to the next workstreams (S104/S1188/
  S3776), where each violation is an independent, compilable change.
- The external verifier's report missed the two newly-introduced Sonar issues
  (its brief covered IBackoffSleeper/NOSONAR/no-real-I/O but not new Sonar
  regressions). The lead caught them by running `make gate` + the live Sonar API
  directly — that is the definitive check, not the verifier's summary.
- iOS *sonar-scanner* leg intermittently fails with a Java "Failed to create
  temp file" (rc=3) — environmental (sandbox `/tmp`), unrelated to C++ source;
  the vehicle-spy **C++** scan succeeds. Flagged, not forced past.
- Remaining untracked (intentionally NOT committed): `claude-teams-bridge/`
  (not part of this workstream).

**Next (per ROADMAP):** vehicle-spy-esp32 sonar-zero (48 OPEN) — same shape as
vehicle-spy; the S3776/god-class ones need blind-TDD gating. `.mm → .cpp`
migration. WiFi provisioning #87. Each as its own prescription + per-violation
commits.

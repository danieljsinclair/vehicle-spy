# TODO — vehicle-sim external-agent bootstrap

> **Operating model.** The lead (Claude) writes prescriptions + tracks the big picture.
> The **user is the go-between** — copies prescriptions to external AI teams outside Claude Code.
> External teams build + self-check (team roles); the lead verifies via "PA" sub-agents only as a last resort.
> Token budget is constrained — the lead's tokens are the most expensive, so delegate everything possible.

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
1. Re-runs `make gate` from a clean state; confirms green.
2. Re-checks the **live SonarCloud API** for the project (OPEN = 0, or the expected delta).
3. For test work: re-runs the affected tests, reports per-test **runtime** + confirms **no real I/O** (grep).
4. For refactors: confirms the characterization tests are still GREEN (behavior preserved).
5. Greps for DRY violations (no parallel fake-clock/seam implementations; no NOSONAR; no skipped tests).
Report the verifier's findings alongside your own. **Trust but verify** — the lead will independently re-check.

---

## REPORTING (what to send back so the lead can verify)

- Commit SHA(s) + message(s) (rule-prefixed, e.g. `cpp:S107 ...`).
- Gate evidence: `make gate` result (test count, ios-analyze clean, sonar-scan ran).
- **Live SonarCloud OPEN count** for the project (before → after).
- For test work: **per-test runtime** (before → after) + confirmation no real I/O.
- Files touched (confirm scope — no stray files).
- Any issue you couldn't resolve (left open + flagged) — do NOT force past a red gate or weaken a test to force green.

---

## PROGRESS (lead-tracked, 2026-07-14)

> Lead delegate-only model: the lead (Claude) does NOT write source code — it
> analyses, briefs a builder subagent (single-writer on `TCPTransport.*` + its
> tests), verifies independently, and runs a separate verifier subagent for the
> self-check protocol. No push, ever.

- [x] **vehicle-spy S107 ctor fix** (`TODO_vspy_s107.md`) — **VERIFIED NOT NEEDED.**
  Checked live SonarCloud API 2026-07-14: `cpp:S107` is **OPEN = 0** on
  vehicle-spy (total OPEN = 0). The TODO.md CURRENT STATE claim ("cpp:S107 is
  now OPEN") is stale. The ctor is exactly 7 params — sits AT the S107
  threshold (fires at >7), so correctly not flagged. Per the S107
  prescription's Step-0 guard ("if S107 is NOT open… stop + report — don't fix
  the wrong thing"), NO `TransportEndpoint` grouping is done as an S107 "fix"
  (that would be churn + a forbidden parameter-object band-aid). The endpoint
  grouping is instead folded into the fast-tests ctor reshape below, where it is
  REQUIRED to keep the ctor ≤7 after injecting `ISocket` + `IClock`.

- [ ] **vehicle-spy fast mock-based tests** (`TODO_vspy_fast_tests.md`) — **IN PROGRESS (delegated to builder subagent).**
  - [ ] Phase 1 — `ISocket` seam (mock network I/O): `include/vehicle-sim/pipeline/ISocket.h`, `PosixSocket` (verbatim port of today's direct POSIX), `FakeSocket` (scriptable), inject into `TCPTransport`, route all fd ops through it.
  - [ ] Phase 2 — extend `IClock`, remove `IBackoffSleeper`/`RealBackoffSleeper`/`InstantBackoffSleeper` + `HuntResilienceConfig::sleeper`; fold 50ms ATI→ATHELO wait + ELM pacing + hunt backoff through `IClock`. Keep ctor ≤7 via `TransportEndpoint`.
  - [ ] Phase 3 — rewrite `TCPTransport*.test.cpp` to `FakeSocket`+`FakeClock`; remove `LoopbackServer`/`HuntingServer` real loopback; assertions UNCHANGED.
  - [ ] Phase 4 — `make gate` green + live vehicle-spy OPEN=0; `IBackoffSleeper*` gone (grep 0); per-test <100ms; no real I/O in tests.
  - Production callers to update for ctor change: `src/pipeline/PipelineFactory.cpp:128`, `vehicle-sim-ios/VehicleSim/VehicleSimWrapper.mm:405`.
  - Single-writer: only the builder subagent edits `TCPTransport.*` + the 4 test files.

# TODO_coverage_local_live_drift.md — local-vs-live coverage denominator drift (DRY fix)

> Read `TODO.md` first. This fixes a **local-vs-live coverage DENOMINATOR drift** — the pipeline's single-source-of-truth is leaking for ≥1 project. **Coverage-pipeline work** (not test-writing, not the esp32 48-issue Sonar backlog). The injection script is SHARED across all 3 projects, so a fix for one can drift another — the verification must sweep ALL THREE.

## SYMPTOM (observed in `make coverage-summary` for vehicle-spy-esp32)
- **Live Sonar:** 59.7% — 1153 / **1930** lines.
- **Local XML:** 64.3% — 1153 / **1794** lines.
- Same **numerator** (1153), denominators differ by **136**. → pure scope/denominator drift, NOT a coverage-data issue. The makefile flags it as "possible scope/upload drift."

## PRIME HYPOTHESIS (evidence-grounded — CONFIRM, do not assume)
The global `.h`-skip added to `scripts/lcov_add_uninstrumented.py` (line ~164, `if file.endswith(('.cpp','.ino'))`, commit `578bf2f` — done for vehicle-spy) diverges from **vehicle-spy-esp32's `cfamily_cpp_suffixes`** in `coverage-manifest.toml`, which **includes `.h`**. Consequence:
- esp32 **cfamily counts `.h` lines** → live denominator 1930 (includes `.h`).
- esp32 **lcov/XML no longer injects `.h`** → local denominator 1794 (excludes `.h`).
- The `.h` decision lives in TWO unsynchronised places (injection-skip vs cfamily-suffix) — that is the DRY leak.

## TASK
1. **CONFIRM the mechanism.** Diff esp32's **live** file/line set (component_tree `measures?metricKeys=lines_to_cover`, strategy=leaves) against the **local** `firmware/build-verify/coverage-sonar.xml` file set. Are the ~136 extra live-lines esp32 **`.h` files**? Name them.
2. **SWEEP all 3 projects** for local-vs-live denominator drift (the `.h`-skip was global — vehicle-spy + vehicle-spy-ios may have latent drift too). For each project report live vs local `lines_to_cover` + `%`. (vehicle-spy was just verified at 74.2% live==local; re-confirm + check ios.)
3. **ALIGN `.h` handling to ONE source of truth.** The agreed lane is "declaration-only `.h` contribute 0 executable lines → counted nowhere." So the consistent fix is: `.h` should be **excluded on BOTH sides for ALL projects** — i.e. drop `.h` from esp32's `cfamily_cpp_suffixes` too (align cfamily with the injection skip). BUT the esp32 suffix list includes `.h` for a reason (`.ino` + `.h` indexing — see the manifest comment) — **verify dropping `.h` from esp32 cfamily doesn't break `.h`/`.ino` indexing/analysis** before doing it. If it does break indexing, the alternative is to make the injection `.h`-skip **per-project** (esp32 keeps injecting `.h` to match its cfamily) — less ideal (keeps the two-place spec) but safe.
4. 🔥 **FIRESTOP (before changing anything):** post the diagnosis (which lines/files, confirmed mechanism) + the proposed alignment option (a: drop `.h` from esp32 cfamily, or b: per-project injection skip) + the consequence analysis (does dropping `.h` break esp32 indexing?). **Get lead sign-off on the approach** before editing the pipeline.
5. **VERIFY after the fix:** all 3 projects **live == local** (denominator + `%`, within rounding); exactly-once still holds; **no hiding** (no excluding executable code to force alignment — declaration-only `.h` are 0-executable, that's the legitimate justification, NOT gaming); `make gate` green; live Sonar OPEN delta = 0 for every project.

## SELF-CHECK + REPORTING (per TODO.md — corrected protocol)
Spawn a verifier teammate that independently: re-checks **all 3 projects** live-vs-local via the measures API, confirms the drift is closed, confirms exactly-once + no-hiding hold, and re-checks **live** Sonar OPEN delta (flag ANY new issue — a cfamily-suffix change can shift Sonar components). Report per-project before→after live + local numbers.

## DO NOT
- Don't push. Don't suppress (no NOSONAR). Don't exclude **executable** code to force alignment (that's the forbidden metric-gaming — only 0-executable declaration `.h` may be dropped). Don't assume the mechanism — confirm via the component_tree-vs-XML diff. Don't change the pipeline before the 🔥 firestop sign-off. Don't touch the esp32 48-issue Sonar backlog (separate workstream).

---

## 🔴 REOPENED — 2026-07-16 (prior CLOSED block was WRONG: built on a transient 17:11 scan; removed)

**The drift is REAL and PERSISTENT.** Current live API (post-reopen check):
- **esp32:** live `lines_to_cover` = **1933**, coverage = **59.8%**; local XML = **1797**. Drift = **+136** (live > local) — the original symptom, still present.
- The earlier "2351=2351" reconciliation was a **transient stale pre-fix scan** (uploaded at ~17:11, same snapshot that showed vehicle-spy snapping back to 7045). It matched only because both sides were stale-pre-fix. **Disregard that reading.**

### Open investigations (see delegated builders)
- **(A) Name the +136:** run live `component_tree` (per-file `lines_to_cover`, strategy=leaves) vs current local XML; identify exactly which files/lines cfamily counts that the XML omits. Then align per the original plan (single-source `.h` handling), 🔥 firestop before any change.
- **(B) vehicle-spy snap-back:** timeline 7045 (15:20) → 6019 (16:01, fix landed) → 6019 (16:41) → **7045 (17:11!)** → 6019 (17:20) → 6019 (now). The 17:11 spike = a stale/pre-fix XML uploaded (same scan as esp32 2351). Determine what ran that scan (pre-merge master scan? stale build dir? race?) and confirm the pipeline is now stable at 6019 — or it's real non-determinism to fix.

### Status of prior conclusions
- The "NO PIPELINE CHANGE / refuted hypothesis" conclusion is **WITHDRAWN** pending the corrected (A) diff. The `.h`-skip / `cfamily_cpp_suffixes` DRY-leak hypothesis remains the prime suspect until (A) names the exact 136 lines.
- Task #1 (merge) remains DONE (commits on master, branches converged).

---

## 🔥 OPTION A — VERIFICATION GATE RESULT: **FAIL (do NOT apply)**

**Mandated checks 1–4, run dry (no pipeline edit applied):**

- **Check #1 (12 shims → lines_to_cover = 0): FAILS by construction.** Inspected `firmware/build-verify/compile_commands.json` (27 CUs, all `.cpp`/`.cc` — 0 standalone `.h` CUs). The 12 shim `.h` are analyzed by cfamily **only because they are `#include`d by a compiled CU**, NOT because they appear in `cfamily_cpp_suffixes`. Removing `.h` from the suffix list leaves them still pulled in via the including CU's preprocessor → cfamily **still counts them live** → the +136 drift **does NOT close**. Verified against compile_commands, not assumed.
- **Check #2 (vanilla .h with real coverage stay): N/A** — A fails at #1. (Same inclusion logic means dropping `.h` from suffix would NOT drop the vanilla `.h` either — so A wouldn't reverse-drift, it simply wouldn't fix the forward-drift. Confirms A is ineffective, not harmful.)
- **Check #3 (.ino analysis unaffected): N/A** — moot; A is rejected.
- **Check #4 (no Sonar issue shifts): N/A.**

**Per the firestop rule ("If check 2 fails → A is wrong; fall back to Option C"):** A is refuted. **FALL BACK TO OPTION C** (single-source — align both sides to one manifest-driven list).

### Why A fails (the real mechanism)
cfamily in Compilation-Database mode analyzes the `.cpp`/`.cc` CUs and **all headers they `#include`** — the suffix list only governs which *standalone* files are treated as roots. Since the 12 shims are never roots (0 CUs), the suffix list is irrelevant to whether they're counted. This matches the vehicle-spy precedent the lead cited: `ITransportOutput.h` indexes at ~50% via compile_commands inclusion **despite `.h` not being in vehicle-spy's suffixes**. So cfamily counts included `.h` regardless of suffix → A cannot close the drift.

### Option C shape (proposed, needs a follow-up firestop)
The drift is **cfamily over-counting** declaration-only `.h` (136 lines) that llvm-cov correctly excludes (0 executable lines). The XML side (post-578bf2f) is *correct*; cfamily is the outlier. Single-source alignment must bring **cfamily's file set to match llvm-cov's executable-line view**, driven by ONE manifest list. Candidate: drive esp32 `sonar.exclusions` (or a derived header-exclusion) from the same manifest source so the 12 declaration shims are excluded from cfamily — matching the XML's 0. (Re-injecting synthetic 0% into the XML is REJECTED — it re-introduces the exact false-0% problem 578bf2f fixed.) Exact C mechanism to be designed + firestopped before applying.

### Stability hardening (B) — APPLIED + VERIFIED ✅
- **Applied:** `sonar-scan` now depends on `coverage-clean`; `sonar-scan-esp32` now depends on `coverage-firmware-clean` (Makefile, 2026-07-16). A stale build dir (leftover XML/.profraw) can no longer be uploaded — every scan regenerates from current source state. No per-file CI yaml exists (only `.github/workflows/ios.yml`, which has no coverage/sonar steps), so the Makefile scan targets are the single enforcement point.
- **3-scan stability check (back-to-back clean-rebuilds):** vehicle-spy LTC = **6019 / 6019 / 6019**; esp32 LTC = **1794 / 1794 / 1794**. Deterministic — no oscillation. The 17:11-style spike was a stale pre-fix XML upload; B's clean-first ordering prevents recurrence.
- NOTE: local esp32 LTC is **1794** (post-B, fully clean), not the user-cited 1797 — a 3-line build-state delta (uncommitted FirmwareApp.cpp/.h in tree may shift line counts). Local artifact is stable at 1794; the +136 live drift (cfamily 1933 vs 1794) persists → Option C still required.

---

## 🔥 OPTION C — DESIGN (proposed, NOT applied; awaiting sign-off)

### 1. Classification of the 136 lines (the non-negotiable gate) — DONE
Sampled all 12 drift files. They are **NOT declaration-only headers** — they are **executable platform-veneer** (Arduino adapter implementations), each 2–13 `override` methods, all gated by `#ifdef ARDUINO`:
- `ArduinoUdp.h`: `begin/beginPacket/write/endPacket` bodies delegating to `WiFiUDP` — real executable statements.
- Same shape across all 12 (`ArduinoHttpServer.h` 11 overrides, `ArduinoWiFi.h` 13, `ArduinoPartition.h` 7, `ArduinoUpdate.h` 5, `ArduinoCrypto.h` 4, `ArduinoSntp.h` 7, `ArduinoPreferences.h` 6, `ArduinoTcpServerClient.h` 7, `ArduinoTcpServer.h` 3, `ArduinoTimeNtp.h` 5, `ArduinoTime.h` 2, `ArduinoUdp.h` 4).
- **Why 0 in the XML:** gated by `#ifdef ARDUINO` → compiled only for the ESP32 firmware build, NOT for host tests → llvm-cov/host-tests give them 0 coverage. cfamily (analyzing the firmware build) counts them.

**Classification result → BRANCH 2 (executable platform-veneer):** per the firestop rule, this is a **declared gap like `.mm`** (two-sided exclusion + scorecard-yellow entry), **NOT a silent exclusion**. Exclusion (Branch 1) is explicitly REJECTED because the lines are executable, not declarations.

### 2. Chosen mechanism (mirrors the existing `[deferred.mm]` policy)
- **Two-sided exclusion:** add a principled glob to esp32 `sonar.exclusions` so cfamily stops counting the 12 shims — matching the XML's current 0. The XML side already excludes them (578bf2f `.h`-skip). Glob, NOT per-file list.
- **Scorecard-yellow tracking:** surface them as a declared gap (new `[deferred.arduino_veneer]` block in coverage-manifest.toml, rendered by coverage_scorecard.py as a yellow warning) — mirrored from `[deferred.mm]`. Tracked, not hidden.

### 3. No-reverse-drift proof
- The 12 shims are the **complete** drift set (verified: live-N-vs-xml-0 = exactly these 12, sum 136).
- The vanilla `.h` that MUST stay counted — `CanBridge.h`, `FirmwareApp.h`, `WiFiManager.h`, `DiscoveryManager.h`, `NtpTimeSync.h`, `StatusLED.h` — are **NOT** in the 12 (none match `Arduino*.h`). So the glob leaves them untouched → they remain counted (no reverse-drift).
- Glob scoping check: 13 `Arduino*.h` exist under firmware; 12 are the drift set; the 13th (`firmware/mocks/ArduinoMock.h`) is **already excluded** via `firmware/mocks/**` → idempotent, hides nothing new.

### 4. EXACT PROPOSED DIFF (not applied)
**coverage-manifest.toml** — esp32 `[[project]]` `exclusions` list, add:
```toml
    "firmware/**/Arduino*.h",              # esp32 Arduino platform-veneer (executable, #ifdef ARDUINO)
                                           # untestable on host; declared gap like .mm (two-sided).
                                           # cfamily over-counts these 136 lines vs the host-XML 0.
```
Plus a new manifest block (mirror of `[deferred.mm]`):
```toml
[deferred.arduino_veneer]
status = "deferred"
resolution = "migrate each Arduino*.h veneer to a thin Arduino shell + .cpp core (PIMPL) so the .cpp counts in vehicle-spy host coverage"
files = [
  { path = "firmware/can-bridge/ArduinoHttpServer.h", lines = 27, owner = "vehicle-spy-esp32" },
  ... (12 entries, lines = the +N from the named diff) ...
]
```
**coverage_scorecard.py** — render `[deferred.arduino_veneer]` as a yellow warning block (reuse `render_deferred_mm` pattern, or add `render_deferred_arduino_veneer`).
**No change to `lcov_add_uninstrumented.py`** (XML side already correct).
**No change to `cfamily_cpp_suffixes`** (Option A rejected).

### Verification plan (post-sign-off, before commit)
- Apply the manifest diff → `make manifest-regen` (byte-stable) → `make coverage-clean coverage-firmware` → `make sonar-scan-esp32`.
- Confirm live esp32 LTC == local 1794 (drift 1933→1794 closed).
- Confirm CanBridge.h/FirmwareApp.h/etc. still counted (no reverse-drift).
- Verifier teammate: re-run live component_tree diff, confirm only the 12 shims dropped, exactly-once holds, Sonar OPEN delta = 0.
- 3-scan stability re-check (B already green).
- `make gate` green; no push.

### Stability hardening (B) — APPLIED ✅ (see above)

### Notes for the recipient
- `firmware/vanilla/FirmwareApp.cpp` + `.h` UNCOMMITTED in working tree (separate esp32 refactor, NOT touched).
- `claude-teams-bridge/` untracked (leftover).
- NOT ready for `git mv` to `docs/archive/` — reopened, active.

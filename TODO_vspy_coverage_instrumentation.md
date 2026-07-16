# TODO_vspy_coverage_instrumentation.md — fix synthetic-coverage injection (declaration headers falsely at 0%)

> Read `TODO.md` first (constraints, gate, delegation model, self-check, reporting). This fixes a **METRIC-CORRECTNESS bug** in the coverage pipeline. It is **NOT** production-behavior work — no C++ source changes. Single-writer on `scripts/lcov_add_uninstrumented.py` (+ the Makefile coverage recipe that invokes it). **Rank this AHEAD of the folder-demarcation workstream** (`TODO_vspy_coverage_hygiene.md`): reorganising folders while the metric is miscalculated polishes a broken gauge.

## ROOT CAUSE (already diagnosed by the lead — VERIFY it, do NOT re-investigate from scratch)
`scripts/lcov_add_uninstrumented.py` appends a synthetic 0%-coverage lcov record for every source file that `llvm-cov export` omits (files with zero executed regions). Two defects in that injection:

1. `count_code_lines()` (lines 43–80) counts **every non-blank, non-comment line** as "executable code" — including `class {`, `public:`, `using …`, member declarations, `= delete;`. It cannot distinguish declarations from executable statements.
2. `generate_lcov_entry()` (lines 158–171) emits `DA:N,0` for `N = 1..line_count` — i.e. **ordinal** line numbers, **not real source line numbers**.

**Consequence:** pure-declaration headers (e.g. `include/vehicle-sim/domain/DemoSignalSource.h` — 45 lines, zero executable code; its `.cpp` is genuinely ~30% covered, lcov `LF:79 LH:24`) get ~22 *fake* "uncovered executable" lines → show **0%** in SonarCloud → AND inflate the project denominator → depress the headline coverage %. There are **~57 such declaration headers** in the vehicle-spy 0% set. (The **11** zero `src/cli/*.cpp` are a **SEPARATE**, genuine population — the CLI entry layer with no unit tests — not this bug; they must stay visible as honest-uncovered.)

**Proof it is synthetic, not real llvm-cov output:** the header's lcov record shows `DA:1,0 … DA:22,0`, marking `#pragma once`, `#include` lines, `namespace`, and `class` as "executable." No real coverage tool marks `#include`/`#pragma` executable. Verify this yourself before fixing (`grep -n 'SF:.*DemoSignalSource\.h' build-cov/lcov.info` + the DA block beneath it).

## THE FIX (recommended approach)
**Coverage must count EXECUTABLE lines only.**

1. **Stop injecting synthetic records for `.h` headers.** llvm source-based coverage ALREADY attributes header coverage correctly when a header has inline code that executes (proven: `include/vehicle-sim/pipeline/ITransportOutput.h` measures ~50% — `SilentOutput` inline covered, default delegators not). A declaration-only header has **zero executable lines** → it must contribute **0 to BOTH numerator and denominator**. That is *correct metric definition*, **not** "hiding untested code" (the forbidden count-zero is about dropping untested **executable** logic — declarations have no executable logic to hide). Concretely: in `find_source_files()` / the injection loop, skip `.h`. (KISS rule. A "skip any file with no executable regions" rule is equivalent but harder; `.h`-skip suffices.)
2. **For the genuinely-uncovered `.cpp`/`.ino` (e.g. `src/cli/*`, `main.cpp`): keep the honest-uncovered injection, but emit REAL source line numbers** — track the physical line number of each counted code line and emit `DA:<physLine>,0`, not ordinals. (Tightening `count_code_lines` to skip braces/pure-declarations is nice-to-have; **real line numbers is the must-fix**.)
3. Preserve everything else: the script's purpose (surface llvm-cov-omitted files so lcov file-list == sonar.sources), the `--exclude` mirroring of `sonar.exclusions`, and symlink-skipping all stay.

## INVARIANTS TO PRESERVE (non-negotiable — the user's hard-won coverage rules)
- **Exactly once:** every production **executable** line counted in exactly one project. No double-count, no miss. (Declaration-only headers now correctly contribute 0 executable lines.)
- **No hiding / no count-zero-to-game:** do NOT exclude files to inflate %. The `.h` change is justified (0 executable lines = nothing to count), NOT an exclusion-for-gain. If you ever exclude a file that HAS executable code → STOP, that is the forbidden metric-gaming.
- **sonar == lcov:** after the fix, per-project `lines_to_cover` must still reconcile between sonar and lcov (re-run the exactly-once audit from `TODO_vspy_coverage_hygiene.md` Phase 3). If dropping `.h` injection creates drift → SURFACE it, do not paper over it.

## 🔥 FIRESTOPS (STOP + report; WAIT for lead sign-off before continuing)
- **🔥 Firestop 0 — sonar-side investigation (BEFORE any pipeline change):** the recommended fix assumes the 0% on declaration headers comes from OUR injected XML, not from cfamily independently counting declaration lines. **VERIFY:** temporarily skip `.h` injection, regenerate `coverage-sonar.xml`, and check via the **live** `component_tree`/measures API whether a declaration header (e.g. `DemoSignalSource.h`) still shows `lines_to_cover > 0` in SonarCloud. **Report which side drives the 0%.** If cfamily independently counts declaration lines, stopping `.h` injection alone will NOT fix the display → STOP + propose the sonar-side handling (do NOT guess; report). **No pipeline change until the lead signs off the approach for whichever side is the source.**
- **🔥 Firestop 1 — post-fix evidence (before declaring done):** post (a) headline vehicle-spy coverage % before → after, (b) the 0%-header-set size before → after (expect ~57 declaration headers → ~0; the 11 cli `.cpp` stay as honest-uncovered), (c) exactly-once audit still holds, (d) per-file spot check: `DemoSignalSource.h` no longer at a *synthetic* 0% (absent from the executable-line denominator or showing true 0 executable lines) while `DemoSignalSource.cpp` still reads ~30%. Lead verifies.

## ACCEPTANCE (all must hold)
1. Declaration-only headers no longer show a SYNTHETIC 0% from phantom executable lines (`DemoSignalSource.h` is not `DA:1..22,0` — it is either absent or has 0 `lines_to_cover`).
2. Synthetic `.cpp`/`.ino` entries use REAL source line numbers (spot-check one `src/cli/*.cpp`: its `DA` line numbers map to actual statement lines).
3. Headline vehicle-spy coverage % is computed over EXECUTABLE lines only (report before → after — **if the number does not move, the fix did not take**).
4. The 11 `src/cli/*.cpp` remain honestly-uncovered (NOT hidden) — their debt is real and stays visible.
5. exactly-once + sonar==lcov still hold (re-run Phase 3 audit).
6. `make gate` green; **live** Sonar vehicle-spy OPEN=0 (coverage-only change must be Sonar-NEUTRAL; any new issue = find the real cause, no suppression). No push. No skipped tests.

## SELF-CHECK (corrected protocol — a prior verifier missed S1709/S2259 by only ticking the brief's boxes)
Spawn a **verifier teammate** (fresh context) that independently:
- Re-runs the full coverage pipeline from clean (`make coverage-clean && make coverage-run`) + regenerates `coverage-sonar.xml`; reports headline % before → after.
- Re-checks the **live** SonarCloud API for vehicle-spy: OPEN-issues **delta** (before → after) — flag **ANY** newly-introduced issue, not just the brief items. A coverage-pipeline change must be Sonar-neutral.
- Confirms exactly-once (component_tree union, no overlap/miss) still holds.
- Spot-checks `DemoSignalSource.h` + `DemoSignalSource.cpp` + one `src/cli/*.cpp` in the regenerated XML.
Report both your result + the verifier's.

## REPORT BACK
- Commit SHA(s) + message(s) — prefix `coverage:` or `fix:`; one logical commit; end `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
- **Headline vehicle-spy coverage % before → after** (live Sonar measures API, not local).
- 0%-header-set size before → after; confirm the 11 cli `.cpp` stay visible.
- Exactly-once audit result (still holds).
- Live Sonar OPEN delta (expect 0 → 0).
- `make gate` result.
- **Firestop 0 finding:** which side drove the 0% + what sonar-side handling (if any) was needed.

## DO NOT
- Don't push. Don't suppress (no NOSONAR). Don't exclude files that have executable code (forbidden metric-gaming). Don't touch production C++ (pipeline-only). Don't start the folder-hygiene workstream — separate, comes after. Don't re-investigate the root cause — it is diagnosed above; verify + fix.

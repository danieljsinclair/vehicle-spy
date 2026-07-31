# TODO_vspy_coverage_hygiene.md — folder-structure-driven coverage demarcation (exactly-once)

> Read `TODO.md` first. **This workstream makes the folder layout the single source of truth for coverage/sonar scoping.** Path globs do ALL inclusion/exclusion from ONE declaration site (the manifest), driving both sonar and lcov. No per-file lists. No filename-pattern globs. No Python build-derivation (separate later workstream).
> Parallel-safe with `TODO_vspy_s107.md` + `TODO_vspy_fast_tests.md` — different files (platform/domain vs TCPTransport). Single-writer per file still applies.

## ACCEPTANCE CRITERIA (the bar — non-negotiable)
**Every production line is counted EXACTLY ONCE, in ONE project only.**
- No duplicate counts (a line appearing in 2 projects).
- No missed files/lines (a prod file in zero projects).
- No overlap between projects.
- Untested code IS counted — but once per line, total.
- **Verify** via the live `component_tree` API: fetch each project's file list (`danieljsinclair_vehicle-spy`, `-ios`, `-esp32`), union them, and assert (a) no file appears in >1 project, (b) every on-disk prod file appears in exactly one, (c) sum of per-project `lines_to_cover` reconciles with total prod executable lines (no phantom/missing).

## FIRESTOPS (lead checkpoints — at each: STOP, report, and WAIT for lead sign-off before continuing)
The lead verifies critical invariants at these points to catch cascading errors early (a bad move → bad globs → bad audit). **Do NOT proceed past a firestop without explicit lead approval.** At each, post the evidence cited; the lead (or a PA) checks it before you continue.

- **🔥 Firestop 0 — pre-move audit (before any `git mv`):** Post the full move/split plan — each file → destination folder + **which build owns it** (the truth: macOS CLI / iOS xcodebuild / firmware). Lead checks no file is moving to the wrong project + that splits keep the majority in the original. **No moves until approved.**
- **🔥 Firestop 1 — after Phase 0 (folders restructured):** Post build-green evidence + `git log --follow <a moved file>` (history preserved) + confirmation all `#include` paths updated + host/iOS/firmware each still compile. Lead verifies the on-disk layout matches the approved plan. **No globs wired until approved.**
- **🔥 Firestop 2 — after Phase 1 (globs wired):** Post a live `component_tree` spot-check — a sample iOS header is OUT of vehicle-spy AND IN vehicle-spy-ios; a macOS header still IN vehicle-spy; the shared core not double-counted. Lead verifies the globs actually scope correctly. **No full audit until approved.**
- **🔥 Firestop 3 — Phase 3 acceptance (exactly-once):** Post the full audit — overlap set = ∅, no missing prod files, per-project `lines_to_cover` reconciles to the total. Lead signs off the exactly-once invariant. **Final gate.**

## DESIGN PRINCIPLE (the rule that governs every decision here)
The **folder layout is the source of truth.** Convention:
- `**/ios/**` → iOS-only → vehicle-spy-ios
- `**/macos/**` → macOS-only → vehicle-spy (the macOS CLI builds it)
- `firmware/**` (real files + `firmware/vanilla/`) → vehicle-spy-esp32
- everything else under `src/` + `include/` → shared host C++ core → vehicle-spy
- `test/**` → tests (excluded from all production coverage)

Adding a file = drop it in the right folder; it's auto-grouped/scanned/included/excluded by the glob. **If a file can't be cleanly path-globbed to exactly one project, RESTRUCTURE it (move or split) until it can.** Never add a per-file entry. Never use a filename-pattern glob (`*iOS*`). Build globs stay in the Makefile (separate concern).

## Phase 0 — PRECURSOR: restructure folders so each project is path-globbable
**Audit first** — for each platform/scattered file, confirm which build actually compiles it (truth = the build, not the filename). Then move/split. **`git mv` for every move (preserve history).** When splitting a file that jumbles macOS+iOS code behind `#ifdef`: **`git mv` the original to its majority-platform home (keeps the bulk + full history), then extract the smaller platform chunk into a new file.** NEVER create two new files and delete the old — that loses history.

Known targets (confirm each via the build before moving):
- `include/vehicle-sim/ble/platform/BLEManageriOS.h` → `include/vehicle-sim/ble/platform/ios/BLEManageriOS.h` (`git mv`)
- `include/vehicle-sim/ble/platform/BLEManagerMacOS.h` → `include/vehicle-sim/ble/platform/macos/BLEManagerMacOS.h` (`git mv`; stays vehicle-spy)
- `src/ble/platform/BLEManageriOS.mm` → `src/ble/platform/ios/BLEManageriOS.mm` (`git mv`; keep next to its `.h`)
- `src/ble/platform/BLEManagerMacOS.mm` → `src/ble/platform/macos/BLEManagerMacOS.mm` (`git mv`)
- `include/vehicle-sim/domain/IOSDBCContentProvider.h` (+ its `.mm`/`.cpp`) → an `**/ios/**` path (e.g. `include/vehicle-sim/domain/ios/`) (`git mv`) — it's scattered in `domain/` today, which is why path globs miss it.
- `BLEManagerMock.h` + `BLEManagerMock.cpp` → `test/ble/platform/` (`git mv`; test-only, referenced only by 2 tests).
- `SilentOutput` (a test double defined inline in the production header `include/vehicle-sim/pipeline/ITransportOutput.h`) → extract to a test header under `test/` (production header keeps only the `ITransportOutput` interface + `StdOut`/`TaggedOutput`). Same "test code in production" smell.
- **Split jumbled `#ifdef` files:** grep shared files for `TARGET_OS_IPHONE` / `__APPLE__` / platform `#ifdef` blocks that mix iOS + macOS code in ONE file. For each: `git mv` the file to the majority platform's folder (history preserved), extract the minority-platform chunk to a new file in its folder.

Update all `#include` paths after moves (production + tests + firmware). Build must stay green.

## Phase 1 — KISS path globs (one declaration; drives sonar + lcov)
Edit `coverage-manifest.toml` per-project globs (the plumbing — `gen_coverage_config.py` → sonar `@GENERATED@` blocks; `manifest_query.py` → Makefile lcov `--exclude-glob` — already exists):
- **vehicle-spy:** sources `src/**`, `include/**`; excludes `**/ios/**`, `**/platform/ios/**`, `test/**`.
- **vehicle-spy-ios:** sources `vehicle-sim-ios/VehicleSim/**`, `include/**/ios/**`, `src/**/ios/**`; excludes `src/**` (shared core owned by vehicle-spy), `test/**`.
- **vehicle-spy-esp32:** sources `firmware/**` (symlinks excluded as today); excludes `test/**`.
- `make manifest-regen` + `make manifest-check` (byte-stable, no drift).

## Phase 2 — two-sided check (no file in limbo)
For each moved iOS file: confirmed EXCLUDED from vehicle-spy (0 occurrences in its sonar file list) AND INCLUDED in vehicle-spy-ios. For each macOS file: still in vehicle-spy, NOT in ios. Run the live `component_tree` per project to prove it.

## Phase 3 — exactly-once audit (the acceptance gate)
Live API, all 3 projects' `component_tree` (strategy=leaves):
1. **No overlap:** intersect the 3 file lists → must be empty (after excluding the intentional shared-core rule: src/ is vehicle-spy-only, dropped from ios).
2. **No misses:** compare the union against the on-disk prod file set (`find src include firmware vehicle-sim-ios -name '*.cpp' -o -name '*.h' -o -name '*.mm' -o -name '*.ino'` minus tests + config artifacts like `OtaPublicKey.h` + the `.mm` two-sided exclusion). Every prod file in exactly one project.
3. **Reconcile:** sum of per-project `lines_to_cover` ≈ total prod executable lines (flag any phantom/missing chunk).
4. vehicle-spy's 0%-header list should shrink (iOS/platform headers left its denominator) — re-pull the `.h` coverage report to confirm.

## Phase 4 — minor hygiene (fold in)
- Drop unused includes if still flagged: `<chrono>` (`TCPTransport.h`), `<functional>` (`TCPTransportHuntingCancel.test.cpp`).

## Self-check (mandatory)
Verifier teammate independently re-runs the Phase 3 exactly-once audit (component_tree union + on-disk diff), confirms no overlap/miss, `make gate` green, and that vehicle-spy OPEN count is unchanged/expected (coverage-only restructure should be sonar-neutral, but a moved file can shift a sonar component — verify via live API). Report both results.

## Out of scope (separate later workstream, only if needed)
"Build-derived sources": derive each project's `sonar.sources` from its `compile_commands.json` file-list (the macOS build doesn't compile `BLEManageriOS.h` → auto-excluded). Folder structure here already achieves future-proofing more simply (KISS); revisit build-derivation only if folder structure proves insufficient.

## Constraints
- `git mv` for ALL moves (history). When splitting: `git mv` original (keep majority + history) then extract the smaller chunk — never two-new-and-delete-old.
- No per-file lists. No filename-pattern globs. Path globs only.
- No push. No suppression. No skipped tests. Full `make gate` green per commit. Commits logically separated (folder moves / glob update / audit), rule-prefixed where a Sonar rule is involved.

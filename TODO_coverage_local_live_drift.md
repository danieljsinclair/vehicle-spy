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

# Coverage .mm Policy (Obj-C++ on SonarCloud)

**Status:** PURE FALLBACK — adopted 2026-07-13 after the suffix-full probe ruled
out cfamily-on-.mm definitively (cfamily rejects every .mm CU as "unsupported",
even with a perfect compile_commands.json; the sibling app has the identical gap).

## The problem

SonarCloud has **no clean Obj-C++ analyzer** on any tier:

- **cfamily** (C/C++ plugin) parses `.mm` AS C++. With the `.mm` suffix trick it
  *does* create a component, but misparses Obj-C message-sends / `#import`,
  emitting permanent false positives (`#import`→`#include` S3805, etc.) + silent
  incomplete symbolic execution. Worse — in Compilation-Database mode it rejects
  `.mm` CUs outright as "unsupported" (0 analyzed), proven by the app's own
  scanner log + a 49-CU probe in vehicle-sim (3 .mm, all "skipped unsupported").
- **Swift plugin** parses `.swift` ONLY — rejects `.mm` (0 components).
- **Coverage-XML-alone** does NOT create a SonarCloud component. A `<file>` entry
  for a `.mm` with no claiming analyzer is silently dropped.

Net: `.mm` files are **count-zero everywhere**. This is confirmed 4 independent
ways (cfamily-misparse experiment, Swift-plugin rejection, coverage-XML-alone,
suffix-full probe).

## The recipe (this repo)

The PURE FALLBACK keeps the headline honest (sonar==lcov for ALL 3 projects)
while tracking `.mm` as a visible, persistent migration roadmap — never hiding
the gap.

1. **Two-sided `**/*.mm` exclusion (glob, zero file names).**
   - `sonar.exclusions` + `sonar.coverage.exclusions` both include `**/*.mm`
     for vehicle-spy + vehicle-spy-ios (see `coverage-manifest.toml`).
   - The sonar-bound coverage XML drops `.mm` too: `lcov_to_xml.py
     --exclude-glob '**/*.mm'` (vehicle-spy, wired in `run_coverage_tests.sh`)
     and `xccov_to_sonar.py --exclude-glob '**/*.mm'` (iOS, wired in the
     Makefile `coverage-ios` recipe).
   - The glob auto-catches past/present/future `.mm` — no per-file edits.
   - The **raw** lcov.info still carries `.mm` per-file for inspection; the
     `.mm` is scoped out of the HEADLINE only, not lost.
   - Both sides must drop `.mm` for the headline counts to agree. Result:
     sonar==lcov EXACT for all 3 projects (drift 0).

2. **`xcodebuild analyze` as the Obj-C++ QUALITY channel.**
   - `make ios-analyze` runs Apple's static analyzer (native Obj-C++ parser,
     zero cfamily false positives). It catches REAL `.mm` bugs cfamily never
     could: leaks `[osx.cocoa.RetainCount]`, dead stores `[deadcode.DeadStores]`.
   - The gate FAILS if any analyzer finding lands on a `.mm`/`.m` (findings on
     `.h`/`.cpp`/`.swift` belong to other projects' analyzers).
   - `ios-analyze` is wired into the commit gate (`make gate` — see below), so
     every commit must pass it. Obj-C++ cleanliness is enforced ongoing.
   - COMPLEMENTARY compiler gate: `GCC_TREAT_WARNINGS_AS_ERRORS=YES` is set on
     the iOS app target (Debug + Release) so compiler warnings on `.mm`/`.m`/`.cpp`
     fail the build too. The analyzer owns leak/dead-store bug classes; `-Werror`
     owns the compiler-warning classes (undeclared selectors, conversions, etc.).
     Both gates together = full `.mm` quality coverage. (Two genuine doc/code
     mismatches in shared C++ headers were fixed to enable this with zero carve-
     outs: OBD2Types.h dropped a misleading `@deprecated`; BLEManagerBase.h
     restored a param name.)
   - The analyzer's derivedData is wiped each run (`find -delete`) so the gate
     is deterministic (a warm Index.noindex = no-op = zero findings printed).

3. **Deferred-.mm = YELLOW ACTIONABLE WARNINGS (glob-derived, every build).**
   - `coverage_scorecard.py` globs `**/*.mm`, attributes each to its owner
     project, and renders each as a yellow `-> migrate to .cpp` warning that
     persists every build until closed. This is the visible migration roadmap,
     not a buried footnote.

4. **Durable fix: migrate `.mm` → thin Obj-C shell + `.cpp` core (PIMPL).**
   - Extract the C++ logic into a `.cpp` (counted in vehicle-spy, host-tested),
     leave a thin Obj-C shell in the `.mm` (Foundation/CoreBluetooth glue only).
   - This is the only path that makes `.mm` content count honestly. The yellow
     warnings close one-by-one as each migration lands.

## NOT adopted (ruled out)

- **Option C (cfamily `.mm` suffix on vehicle-spy):** indexes 1-of-4 `.mm` at
  the cost of +8 OPEN (5 permanent false positives) + a shared-header S1117
  side-effect. Reverted 2026-07-13.
- **NOSONAR suppression:** never (standing policy). The two-sided exclusion is
  NOT suppression — it's an honest "this file is uncountable by any analyzer,
  tracked as a declared gap," not a per-line hide.

## Adoption notes for engine-sim-app (same .mm gap)

This recipe is vehicle-sim-specific in implementation (manifest + Makefile), but
the **pattern** transfers to the app (which has the same `.mm` gap — its
`EngineSimWrapper.mm` is "skipped unsupported" by cfamily too):

1. Two-sided `**/*.mm` exclusion (the app already excludes
   `EngineSimApp/EngineSimWrapper.mm` from coverage by leaf-name — generalize to
   the glob).
2. `xcodebuild analyze` as the quality channel (the app already has a compile-DB
   scraper; add the analyze target + log-scan gate). Wire it into a `make gate`
   target so it runs every commit (vehicle-sim's `gate` target chains
   `test firmware-host-tests ios ios-analyze firmware sonar-scan`).
3. `GCC_TREAT_WARNINGS_AS_ERRORS=YES` on the app target (compiler-warning gate
   complementing the analyzer). Fix any doc/code mismatches it surfaces rather
   than carve out.
4. Yellow deferred warnings in the app's coverage scorecard.
5. Migrate `EngineSimWrapper.mm` → thin shell + `.cpp` (the app's own
   `sonar-objcxx-mm-gap.md` memory already names this as the durable fix).

The manifest/generator stay vehicle-sim-specific; the recipe transfers by
knowledge.

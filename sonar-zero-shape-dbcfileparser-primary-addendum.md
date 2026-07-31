# DBCFileParser Primary Shape — Critical Addendum (test-arch2)

**Author:** test-arch2 (Opus)
**Date:** 2026-06-30
**Reads:** the existing `sonar-zero-shape-dbcfileparser-primary.md` (already on disk) + the
live `src/domain/DBCFileParser.cpp` on `sonar_fixes`.

## Why an addendum and not a replacement

A primary-shape document already exists at
`sonar-zero-shape-dbcfileparser-primary.md`. I read it end-to-end and **agree with its core
recommendation**: Alternative A (Extract Method) over Strategy (B) and Cursor (C), with the
8 blind contracts (commit `e606778`, now landed and 903-green) as the guard net. Rather than
overwrite correct prior work with a near-duplicate, this addendum records four points where my
independent read of the source sharpens or corrects it. Treat the existing doc as the base;
fold these in.

## Independent confirmation (what I verified fresh against the source)

- S3776 sites are exactly `parseSignalDefinition` (`:36`, cc56) and `parseString` (`:246`,
  cc52). Confirmed.
- `parseOneValueEntry` (`:147`), `parseValueEntries` (`:175`), `buildResult` (`:191`) are
  **already** cleanly decomposed (single-exit, single SRP). The base doc lists
  `parseOneValueEntry` and `buildResult` under "Functions with Issues" — that overstates it;
  only `parseSignalDefinition` and the `VAL_`/`BO_` arms of `parseString` carry residual
  S3776/S134 pressure.

## Four sharpening points

### 1. Name the one real DRY win (shared uint16-id reader)

The base doc's Alternative A extracts `parseBoLine` and `parseValLine` separately. That's
correct but misses the **specific duplication** that justifies a *shared* helper: the `BO_`
arm (`:262`–`:273`) and the `VAL_` arm (`:282`–`:293`) both perform the identical sequence —

> skip marker, skip spaces, read whitespace-delimited token, `std::from_chars` into `uint16_t`.

Extract a single `parseTrailingUint16(line, afterMarker, canIdOut) -> bool` and call it from
**both** arms. This is the cleanest DRY win in the file (removes ~12 lines of near-duplicate
cursor code) and is independently guarded by blind tests #7 (`MessageHeaderWithNonNumericId`)
and #8 (`ValueTableWithNonNumericIdIsIgnored`). The existing doc's two separate helpers would
leave the duplication in place under two names.

### 2. The ASSERT guidance is too aggressive — narrow it

The base doc's "Convert to ASSERT ... after successful delimiter confirmation (e.g., after
`expect(':')` succeeds)" is **too broad** and slightly conflicts with the task constraint.
After `expect(':')` succeeds there is nothing meaningful to assert — the next operation is
already a read with its own guard. Sprinkling ASSERTs there yields no-op checks.

Per the constraint (parser over **external DBC input**; impossible-by-grammar states only),
the ASSERT-worthy invariants are **internal**, not input-shaped:

- The one genuine candidate is in `buildResult`: a `ParsedSignal` reaching the grouping step
  must have had its `canId` set by a successful `BO_`→`SG_` pair. Given the dispatch order
  (`currentCanId == 0` → `continue` at `:275`), a signal with canId 0 cannot enter
  `signals`. That invariant, if asserted, belongs in `buildResult`, not in the cursor walk.
- Everything else — missing `:`, non-numeric startBit, malformed `(...)`, bad VAL_ id — is
  **external input** and stays a runtime guard (`return false` / `continue`). Do not ASSERT.

### 3. `buildResult` is not a target

The base doc lists `buildResult` under "Functions with Issues" then defers it. To remove
ambiguity: `buildResult` is **not flagged by Sonar** and is already a clean two-phase
(ValueTable-apply, then group-by-canId) single-exit function. Leave it untouched. The only
edits to it should be the optional ASSERT in point 2.

### 4. One precondition before touching the multiplexor arm

The blind test net covers the `'M'` (uppercase) multiplexor marker only. The lowercase
`'m'` + digits variant (`:53`–`:55`) is **uncovered**. Before refactoring the multiplexor
arm, add the `m3` case (test-arch2 flagged this gap in the gate report). Both arms feed the
same `:`-expectation, so a single combined test suffices, but it should exist before the arm
is decomposed.

## My recommendation (independent, confirms the base doc)

**Shape A (Extract Method), with the shared `parseTrailingUint16` from point 1.**

- Phase 1 (parseString, cc52): extract `parseTrailingUint16` (shared BO_/VAL_), then
  `parseValEntries-arm` reducer; flatten the dispatch.
- Phase 2 (parseSignalDefinition, cc56): extract named field readers — `skipMultiplexor`,
  `parseStartBitAndLength`, `parseByteOrderAndSign`, `parseScaleOffset`, `parseMinMax`,
  `parseUnit` — each `(line, pos&, out&) -> bool`, sequenced as a flat guard-clause list.
- Defer Strategy (B) and full Cursor (C). Promote to Cursor only if a **third** duplicated
  cursor site appears (my read: none exists today; the shared uint16 reader absorbs the only
  real one).

**Constraint checklist:** SRP (one field per reader), DRY (shared uint16 reader), KISS (no new
types/dispatch table), single-exit (each reader returns at the bottom), NO NOSONAR, external
input stays as runtime guards, one internal ASSERT in `buildResult` at most.

## Net for team-lead

The existing primary doc is sound; adopt its Alternative A, add the shared uint16 reader
(point 1), narrow the ASSERT scope to `buildResult` internals (point 2), drop `buildResult`
from the target list (point 3), and add the `m3` multiplexor test before that arm is touched
(point 4). The 8 blind contracts + existing happy-path suites form a sufficient net for this
shape. Reconcile with the researcher's third opinion before the gated refactor proceeds.

I have not edited any source. Standing by.

---

**Sources informing the recommendation:**
- [Transform Labs — Refactoring Using Cognitive Complexity](https://www.transformlabs.com/blog/refactoring-using-cognitive-complexity)
- [SonarSource — Refactoring with Cognitive Complexity (webinar)](https://community.sonarsource.com/t/webinar-refactoring-with-cognitive-complexity/45331)
- [mireo/can-utils — C++ DBC parser reference](https://github.com/mireo/can-utils/blob/main/dbc/README.md) · [Qt 6 QCanDbcFileParser](https://doc.qt.io/qt-6/qcandbcfileparser.html)

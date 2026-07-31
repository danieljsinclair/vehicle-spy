# SonarCloud RCA — ESP32 "22" = 1 OPEN + 21 REMOVED (S5421), + the real precedent

**Project:** `danieljsinclair_vehicle-spy-esp32` (org `danieljsinclair`)
**Investigated:** 2026-07-20 · live SonarCloud API (`SONAR_TOKEN_ES`) + local repo
**Local HEAD at time of investigation:** `c8e163b` on branch `sonar_fixes`

---

## TL;DR (corrected, evidence-backed)

- The local summary `sonar: open 1 / total 22` is **correct and live** — by deliberate
  design it computes `total = OPEN + REMOVED` (see `scripts/sonar_live.py`,
  `scripts/build_summary.py:419`). The 21 are `cpp:S5421` on
  `firmware/can-bridge/can-bridge.ino`, all closed in the **zero-file worktree scan**
  `d1532f5e` (2026-07-19T18:38 UTC) as `resolution=REMOVED`.
- REMOVED is **terminal server-side state**. There is **no API to purge, delete,
  transition, or re-open** a CLOSED/REMOVED issue.
- A fresh re-scan / re-merge / re-scope does **NOT** clear them. Proven: the file is
  back in analysis scope (the live S954 sits on it) yet all 21 still carry
  `lastChangeAnalysisUuid=d1532f5e`, `updateDate=2026-07-19T18:38` — untouched by 12+
  later clean scans.
- The "I've seen this before" memory is real but refers to a **different** incident:
  a **coverage denominator drift** from a stale pre-fix XML upload
  (`TODO_coverage_local_live_drift.md`, ~07-16). Its fix was **clean-first re-scan +
  pipeline hygiene** (`6b50ddd`, coverage-clean deps) — NOT a project delete/recreate.
  That playbook does **not** transfer here, because REMOVED is not a re-upload bug.
- **Only delete+recreate the project reliably clears the 21.** Proven the token can
  delete (HTTP 204) and the scanner auto-recreates the same key/name/org. But it is
  destructive (loses 259 FIXED history + any UI-set project config) and there is **no
  documented prior use of it in this repo** — so treat it as the nuclear option, not
  an established habit.

---

## Evidence

### 1. The 21 differ from the 259 by `resolution` (field-by-field)

| | The 21 stuck | The 259 clean |
|---|---|---|
| resolution | **REMOVED** | **FIXED** |
| how closed | zero-file worktree scan `d1532f5e` couldn't see the file → "code gone" | real scans indexed the file, saw the fix |
| created→closed | same day 2026-07-19 16:18→18:38 | across real fix scans 2026-07-20 |
| counted by dashboard? | yes (OPEN ∪ REMOVED) | no |
| counted by local summary? | yes (`total = open + removed`) | no |

One issue's changelog (`AZ97K9B0b2aqX1bLmVd5`) shows the sequence: FIXED at 17:33 →
**REOPENED at 18:37** → REMOVED at 18:38 by the 0-file scan. It has been frozen ever since.

### 2. The "total 22" is real, live, uncached — by design

`scripts/sonar_live.py:126` `fetch_removed_report` queries `resolutions=REMOVED`.
`scripts/build_summary.py:419` `total = open_count + effective_removed`.
`scripts/sonar_summary.py:~193` comment: *"summing the two makes the breakdown match
the dashboard severity widget, which counts OPEN union REMOVED issues."*

The whole `sonar_live` module is architected to **never read a disk cache** (its
docstring: "design-out-staleness... architecturally impossible"). So the local `total`
is a faithful, fresh reflection of genuine server state, not a stale local number.
This is why distrusting "it's just cosmetic" is reasonable — your tooling was built to
surface REMOVED as "not cleanly resolved."

### 3. REMOVED is terminal — proven, not assumed

```
do_transition=reopen        → 400 "Transition from state CLOSED does not exist: reopen"
do_transition=falsepositive → 400 "Transition from state CLOSED does not exist: falsepositive"
bulk_change (all 21, reopen)→ 200 {"total":21,"success":0,"ignored":21,"failures":0}
/api/issues/delete          → 404 "Unknown url"
```

All 21 REMOVED still point to `lastChangeAnalysisUuid=d1532f5e` and
`updateDate=2026-07-19T18:38:00Z` — **not touched** by the 12+ real scans on 2026-07-20
that re-indexed `can-bridge.ino` (the live S954 on that file proves it's in scope).
Therefore a fresh scan will NOT re-evaluate or clear them (confirmed independently by
the researcher: a re-scoped file creates NEW issues, it does not reopen REMOVED).

### 4. Only one branch on SonarCloud — master. All scans hit it.

`api/project_branches/list` → only `master` (main). CE activity shows every scan
(good and bad) ran on `MAIN`. Local branches (`sonar_fixes`, `coverage/vehicle-spy*`)
**do not set `sonar.branch.name`**, so they all upload to the same `master` project
branch. The 21 REMOVED live on `master` alongside everything else — that's why they
still count. **No branch choice, merge, or re-scan clears them.**

### 5. The real "we've seen this before" precedent (different mechanism)

`TODO_coverage_local_live_drift.md` (REOPENED block, 2026-07-16): a **transient stale
pre-fix scan (~17:11)** made local and live coverage/Issue numbers snap back to old
values. The fix was **stability hardening**: `sonar-scan` depends on `coverage-clean`,
`sonar-scan-esp32` on `coverage-firmware-clean` (Makefile, commit `6b50ddd`, 2026-07-16),
plus a 3-scan determinism check (6019/6019/6019). That is a **re-upload-of-wrong-data**
problem, self-correcting via a clean re-scan. It is NOT the REMOVED-issue problem.

Git + doc search for an actual **delete/recreate** of the SonarCloud project: **none
found** (only `6b50ddd` "stale-scan hole" and unrelated S8379/S1820 commits). So the
delete+recreate is **not** an established repo pattern — your recollection conflates
the coverage-staleness incident with this one.

---

## Answers to the RCA questions

**Q1 — Why do REMOVED issues persist in the breakdown? Corruption?**
Normal SonarCloud behavior. REMOVED = the issue's anchor file left analysis scope.
It is retained for history until purged. The breakdown (OPEN ∪ REMOVED) and the OPEN
metric legitimately disagree. Not corruption.

**Q2 — API/admin action to PURGE the 21?**
None. No delete endpoint; no transition out of CLOSED; bulk_change no-ops. The only
purge path is the automatic 30-day DB cleaner (`sonar.dbcleaner.daysBeforeDeletingClosedIssues=30`),
which you've rejected and which is unreliable here (the file is back in scope — whether
the cleaner expires an in-scope REMOVED is unproven and doubtful given re-scans don't
re-evaluate it).

**Q3 — Would a fresh scan self-correct?**
No. REMOVED is terminal; re-scoped files get NEW issues, not reopened old ones. Proven
by the frozen `d1532f5e`/`2026-07-19` on all 21 despite later in-scope scans.

**Q4 — Is delete+recreate the only reliable fix?**
Yes, it's the only thing that **reliably** clears the 21. It is proven to work (same
key/name/org recreated by `make sonar-scan-esp32`). But: it is destructive (loses issue
history + UI config), and there is **no prior documented use** in this repo — so it is
the nuclear option, not a habit.

**Q5 — Is the 1 S954 real?**
Yes per the rule, low value: `can-bridge.ino` lines 18-20 place `struct` forward
declarations before the `#include` block (22-32). Cosmetic; try relocating the 3
forward decls below the includes, or document as a deferral.

---

## Recommended fix (in order)

1. **Decide on delete+recreate honestly, knowing it's destructive and unprecedented here.**
   If the 21 ghosts are unacceptable and you won't wait (the 30-day auto-purge is
   unreliable for in-scope files), delete the project then `make sonar-scan-esp32` for a
   clean 0–1 issue result. First check the quality-gate binding / new-code period aren't
   customized in the UI (they reset on recreate).
2. **Fix or defer the S954** in the next scan.
3. **Branch integration (separate workstream):** merge/consolidate `sonar_fixes` +
   `coverage/vehicle-spy*` — but note this does NOT affect the 21 (all branches hit
   `master`). The `VehicleSimWrapperProtocol` "extract" exists as a **duplicate commit**
   on both `sonar_fixes` (`87dcce0`) and the coverage branches (`80f2047`) — will conflict
   on merge; resolve by diffing both copies.

---

## Root cause (one line)

A git-worktree scan (`d1532f5e`) indexed 0 files, so SonarCloud marked all 21 S5421 on
`can-bridge.ino` as REMOVED. The code was independently cleaned afterward, so nothing
re-fires — the 21 are terminal historical echoes on `master`, awaiting either the
unreliable 30-day purge or a manual delete+recreate.

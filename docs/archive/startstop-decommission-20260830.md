# START/STOP decommission handover — engine-sim-cli.startStop

Date: 2026-08-30

## Purpose

The working folder `/Users/danielsinclair/vscode/engine-sim-cli.startStop` is being archived to `~/vscode/bak/`; this branch preserves everything that would otherwise only exist in that folder or in session memory.

**Consumers:** whoever next works on vehicle-sim (vehicle-spy), engine-sim-bridge, or engine-sim-cli.

## End state (ceremony outcome, 2026-08-30 — all verified by ls-remote)

- escli master `6372720`: a single `--no-ff` merge commit over `2d66d65` + integration `29ee362` (the `feat/startStop` line: `5de66c5` plus integration work). The merge message records the bridge-pin rationale and the vehicle-sim de-registration inheritance.
- `feat/startStop` DELETED both local and remote, only after git-verified `git branch -d` with ancestry proof for BOTH `5de66c5` and `29ee362`.
- bridge gitlink in escli master = `22f6a1c` (`22f6a1cd96d67f04976e8a9b5bb61987de404196` = bridge `origin/master`), so the pin is exactly their published master.
- vehicle-sim has NO gitlink in escli master — de-registration per their `a4363b6` honored; the dependency is intentionally unpinned.
- vehicle-sim master at `894d7f32ee727becc1c706403d4e11594f92d587` — the base sha of this branch.
- On-disk `engine-sim-cli/vehicle-sim` is a git WORKTREE pinned at `b35aeaa`, untracked, byte-clean, content fully pushed (see TODO 9 for the post-move `git worktree repair` note).
- The other team was ACTIVELY PUSHING new branches at ceremony time: `feat/startfrom-instant-skip` and `feat/startstop-release-cut` were observed on the vehicle-sim remote mid-ceremony — expect movement on their side.
- The definitive record is the remotes themselves.

## TODO registry (landmines and open threads)

1. vehicle-sim Makefile `update-dbc` target (around lines 372-378) is ARMED: it checks out master in external/opendbc, pulls, then copies tesla_model3_party.dbc OVER `resources/dbc/Model3CAN.dbc` — replacing the good aggregate DBC (which contains VCLEFT_brakeLightStatus BO_ 994 and DI_gear BO_ 280 needed by start/stop). The echo claims verbatim copy, which is false. Correct regeneration: `scripts/gen_aggregate_dbc.py`. Never run `make update-dbc`.
2. `vehicle-sim/dbc/Model3CAN.dbc` (886B) is vestigial; the real one is `resources/dbc/Model3CAN.dbc`.
3. engine-sim-cli: `tesla_y_performance.mr` is referenced by four engine configs (C63_TeslaY family works; v8_gm_ls_TeslaY, ferrari_f136_TeslaY, CadillacEscalade2024_6-5_V8_TeslaY are blocked) — the file was another agent's never-committed WIP and does not exist in any remote; obtain or regenerate it to unblock those configs.
4. engine-sim-bridge: StubTimeline does not implement `getStartFromS()` — IReplayTimeline gained the pure virtual in b742e6e; any consumer bumping the bridge past 22f6a1c must implement it (parent app's timeline already does).
5. Sonar: bridge project carries 21 pre-existing open issues on the sliplock-tune line (authored before our merges; ownership sits with that line's authors) — do not treat them as regressions. SONAR_TOKEN 401s on issues/search; use SONAR_TOKEN_ES. The shared SonCloud tracker reflects whichever sibling checkout scanned last; the org sits near the 50,000-LOC cap so concurrent sibling scans can fail spuriously.
6. Parent Makefile test-staleness trap: the test check ignores the submodule pointer — a stale `build/cli-test-results.xml` makes `make test` a no-op; delete it to force a real run.
7. Parked design question: live telemetry maps an EMPTY gear_selector to DRIVE (old-schema CSV compatibility), so a capture whose selector decodes late starts the engine without a brake press; decide DRIVE vs NEUTRAL for the live path. In real driving the selector decodes ~120ms after connect, so impact is minimal.
8. USB CAN adapter contract: dumb raw serial 115200 8N1, frame = %03X CAN-ID + space + %02X per byte; no handshake, no version check, timestamps host-added. The 290dd35 firmware floor applies to TCP mode only. WiCan device is on the roadmap — never break the IFrameSource seam's drop-in property.
9. Post-archive repair: `engine-sim-cli/vehicle-sim` inside the moved folder is a git WORKTREE — after the folder moves, run `git worktree repair` (from its parent repo) if git commands inside it fail.
10. Raw capture format `<timestamp_ms>,<verbatim line>` is a frozen contract; live runs write BOTH raw.txt and csv; file replay writes csv only.
11. **vehicle-sim depth-dependent resolver bug (hand to the vehicle-sim team — do NOT fix from this repo):** `src/util/ExecutablePath.cpp` `resolveResource` has no absolute-input short-circuit, and its upward walk caps at 8 directory levels, while `loadVehicle` double-resolves. In checkouts 8+ levels deep the DBC path resolves to `exeDir + '//abs/...'` garbage, parse returns empty, and 32 of 1341 tests fail (all loadVehicle tesla paths). Their pin `b35aeaa` passes only in shallow checkouts (their clone is 6 levels; proven by controlled /tmp depth replicas; their commit claimed full green). No fix exists on their master. The archived copy in bak/ sits at the same depth and will show the same 32 reds — that is the bug, not a stale archive.
12. `roadTest.sh` references `../escli.vehicle-sim` as a sibling — a layout assumption of the other team's convention (vehicle-sim expected beside the tree, unpinned). Documented here; do not alter.

## Salvage branches

- **vehicle-spy (vehicle-sim): `salvage/park-iserial-wip`** (`da785d97fb01bb9f9b109b8e6de12bdd92c5df2e`) — parked firmware park-ISerial debug wiring WIP from 2026-07-10..07-28, rescued from unreachable objects, review before any use.
- **engine-sim-bridge: `salvage/replay-provider-orphans`** (`0abf8d71d5b3fd3e35c09419135047d426386f06`, contains `253565e`) **and `salvage/replay-provider-orphans-2`** (`49c11ca5c2cfed3fdbb3853630ba81e334cc52ea`) — amend-orphans from 2026-08-29; unique delta is a comment block in ReplayTelemetryProvider; superseded by the merge line, kept for the record. Two branches because `49c11ca` is disjoint from the `253565e..0abf8d7` line.

## Folder-only assets (live in bak/ after archive, not in any remote)

- **captures/ at vehicle-sim/captures:** 325 files (`ls | wc -l`), 1.1G total (`du -sh`; 1,119,464 KiB) — 1.1GB of gitignored raw captures; read-only by policy. md5 of `gt-coldstart.csv` = `77fe7ea6774cf8c3d8a21bb484a33e12`; md5 of `gt-live-input.csv` = `c73fe2a5c49a104c72c407ce4c21ddc4`.
- **handoff/tmp-evidence-20260830/:** 13 files of session evidence from /tmp; all EXCEPT `vsim_live_err3.log` (64MB — 64,388,579 bytes, md5 `9836cf5a5ca4e046cb444c7b4240de21`) are committed alongside this doc; the 64MB log stays folder-only, referenced here. Follow-up correction: four of these files (`ab_step2_live.log`, `ab_step2b_live.log`, `before-live.log`, `after-live.log`) initially fell to the repo's `*.log` gitignore rule and were absent from the original commit; they were force-added in a follow-up commit on this branch, making the all-except claim above true from that commit onward.
- **handoff/torque-converter/:** WIP sources and data — NOW COMMITTED on this branch under `handoff/torque-converter/` (see below), previously existed only in the folder.

## Not wired

- `handoff/torque-converter/tmp-tcwip` sources are parked drafts not referenced by any CMakeLists.

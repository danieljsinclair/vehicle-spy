# TODO: Sonar-Zero — ESP32 Firmware (non-S5421 issues)

**One-line focus:** Drive `vehicle-spy-esp32` SonarCloud OPEN to **0** on the **26 non-S5421** issues, one rule-group per commit, blind-TDD for complex ones, gate-green + live re-scan confirming zero before any commit. (`.ino`→vanilla extraction is **done** — not in scope. S5421 is **user-gated / out of scope**.)

---

## 📊 LIVE SONAR STATE (queried 2026-07-16, SonarCloud API)

| Project | OPEN | Notes |
|---------|------|-------|
| vehicle-spy (C++) | **0** | clean |
| vehicle-spy-ios | **0** | clean |
| **vehicle-spy-esp32** | **48** | = 22× `cpp:S5421` (OUT OF SCOPE) + **26× non-S5421** (the target) |

Re-verify before every commit with the live API (do NOT trust cached reports or this doc):

```sh
# Total OPEN
curl -s -u "$SONAR_TOKEN_ES:" \
  "https://sonarcloud.io/api/issues/search?componentKeys=danieljsinclair_vehicle-spy-esp32&statuses=OPEN&ps=1"

# Rule breakdown
curl -s -u "$SONAR_TOKEN_ES:" \
  "https://sonarcloud.io/api/issues/search?componentKeys=danieljsinclair_vehicle-spy-esp32&statuses=OPEN&facets=rules&ps=1"
```

### 🚫 OUT OF SCOPE — `cpp:S5421` (22× mutable-globals), USER-GATED

These 22 `cpp:S5421` mutable-globals findings are **explicitly deferred by the user**. They are **not** on any critical path and **must not** be folded into this work unless the user lifts the gate. (Prior framing that called S5421 "the critical path, only resolves via extraction" was wrong on two counts: extraction is already done, and S5421 is user-gated — see memory `esp32-ntp-routing-sonar-neutral`.)

### ✅ IN SCOPE — the 26 non-S5421 OPEN issues

**By rule** (live):

| Rule | Count | Class | Notes |
|------|-------|-------|-------|
| `cpp:S5008` | 8 | mechanical | C-style / `reinterpret_cast` type-punning |
| `cpp:S2209` | 6 | mechanical | redundant cast / self-assignment |
| `cpp:S5945` | 2 | mechanical | C-string where `std::string` belongs |
| `cpp:S1905` | 2 | mechanical | redundant cast / unused |
| `cpp:S5276` | 2 | mechanical | redundant qualifier |
| `cpp:S1313` | 1 | **security** | hardcoded IP — verify intent before changing |
| `cpp:S3642` | 1 | **security** | weak/hardcoded secret (crypto) — verify intent |
| `cpp:S3732` | 1 | **structural** | recursion in ctor/destructor |
| `cpp:S1820` | 1 | structural | static fn only used in anon-namespace |
| `cpp:S1238` | 1 | mechanical | include-order |
| `cpp:S1135` | 1 | cleanup | TODO/FIXME |

**By file** (live hotspot order — where the work actually lives):

| Count | File |
|-------|------|
| 9 | `firmware/vanilla/ArduinoWiFi.h` |
| 5 | `firmware/can-bridge/can-bridge.ino` |
| 4 | `firmware/vanilla/CanBridge.h` |
| 3 | `firmware/can-bridge/ArduinoSntp.h` |
| 2 | `firmware/vanilla/OtaUpdateServer.h` |
| 1 | `firmware/vanilla/FirmwareApp.h` |
| 1 | `firmware/can-bridge/ArduinoPartition.h` |
| 1 | `firmware/can-bridge/ArduinoUpdate.h` |

> Note: several hotspots (`ArduinoWiFi.h`, `CanBridge.h`, `OtaUpdateServer.h`, `FirmwareApp.h`) live under `firmware/vanilla/` and **are host-testable** — fixes there must keep host tests green. `firmware/can-bridge/*.h` / `*.ino` entries are ESP32 platform veneers/sketch (Arduino API surface) and only build via the firmware toolchain.

---

## ✅ REPO STATE (live, verified 2026-07-16)

- **Branch:** `master` (HEAD `b8c2ea1` "Merge sonar_fixes into master"). `sonar_fixes` is merged.
- **`.ino`→vanilla extraction is COMPLETE.** All manager logic already lives in `firmware/vanilla/`:
  - `FirmwareApp` owns `WiFiManager`, `DiscoveryManager`, `NtpTimeSync`, `CanBridge`, `AtCommandDispatcher` (see `firmware/vanilla/FirmwareApp.h` lines 176-180).
  - `OtaUpdateServer` is a deliberate thin-veneer in `ota_update.ino` delegating to vanilla (logic deleted inline in extraction Stage 5, task #41; host-tested 219/219).
  - `firmware/can-bridge/` is symlinks to `firmware/vanilla/` + a handful of ESP32 platform adapters (HardwareStatusLEDOutput, ArduinoSntp, ArduinoCrypto, ArduinoTcpServer*, ArduinoUdp, ArduinoHttpServer, ArduinoUpdate, ArduinoPartition, ArduinoPreferences) + the two sketch entry points (`can-bridge.ino`, `ota_update.ino`).
  - All four managers have host tests: `firmware/tests/{CanBridge,AtCommandDispatcher,OtaUpdateServer,FirmwareApp}_test.cpp`.
- **Nothing remains to extract.** Do not re-do extraction; do not "wire CanBridge into FirmwareApp" (already wired). This doc previously prescribed that work — it is obsolete and was stripped.

---

## 🎯 METHOD (how to clear the 26 non-S5421 issues)

1. **Triage by rule-group, one commit per group** (memory: `commit-grouping-policy`). Suggested order — mechanical fruit first, security/structural last:
   - **Mechanical (group A):** `S5008`, `S2209`, `S1905`, `S5276`, `S5945`, `S1238` — low-risk casts/qualifiers/strings/include-order. Commit prefix `cpp:SXXXX`.
   - **Cleanup (group B):** `S1135` (resolve the TODO/FIXME, don't delete the marker without addressing intent).
   - **Security (group C):** `S1313` (hardcoded IP), `S3642` (hardcoded secret) — **verify intent first**. A hardcoded IP may be intentional (e.g. device fallback); a "secret" may be a public OTA key. Present the finding + context to the lead before changing; do not blind-edit crypto boundaries.
   - **Structural (group D):** `S3732` (ctor/dtor recursion), `S1820` (anon-namespace static). Small but verify no behavior change.
   - **`S3776` LAST** (cognitive complexity) — **not currently in the 26** but if any function grows complex during a fix, it goes last with a VIEW (see checklist).
2. **TDD for anything non-mechanical:** blind spec-first where behavior is involved (red that compiles → green). Pure cast/qualifier fixes need no new test but must not regress existing host tests.
3. **Commit gate per commit** (memory: `hard-commit-gate-no-push`): full `make gate` green (incl. `firmware-host-tests`, `ios-analyze`, firmware `.bin` build) **+ live Sonar re-scan confirming OPEN dropped with no new issues**. Run the scan BEFORE committing, confirm the count, not after. **Never push** — user reviews all commits.

---

## 🔗 SEPARATE WORKSTREAMS (reference only — do NOT fold in)

- **`.mm` → cpp migration** — iOS/bridge coverage work; its own track.
- **WiFi provisioning (#87)** — AP-mode default + app/API-set SSID; pending task, separate scope.

Point at them; do not prescribe them here.

---

## ✅ PROCESS CHECKLIST (concise — full rationale in memory + TODO.md)

- [ ] **TDD blind spec-first** for behavioral changes (spec, not implementation); RED phase must compile.
- [ ] **One builder at a time** (serialized); one editor per file.
- [ ] **`S3776` last**, with a VIEW (alternatives + web cites + recommendation) before refactoring.
- [ ] **Proof-of-life ACK** as the FIRST instruction on every Agent spawn; **explicit `model`** on every spawn.
- [ ] **No pushes ever** — user reviews all commits; commits stay unpushed.
- [ ] **No `NOSONAR` / suppression, ever** (memory: `no-sonar-suppression-ever`). Real fix or leave open.
- [ ] **No skipped tests** (memory: `no-skipped-tests-audit-one-probe-to-remove`).
- [ ] **Verify independently** — never trust a "green" claim; run `make gate` + the live Sonar API yourself.
- [ ] **Commit gate = full green + live Sonar OPEN=0 movement + no new issues**, per project.

---

**Remember:** the goal is **Sonar-zero on the 26 non-S5421 esp32 issues**. Extraction is done, S5421 is user-gated, the other two projects are already at OPEN=0.

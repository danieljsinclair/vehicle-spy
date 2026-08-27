# PLAN: Credential-Independent Firmware (NVS-based WiFi provisioning)

Planning-only. No code, no builds, no device. Goal: any built firmware runs on
any network; creds + token live in NVS; `make`/`make flash` need no secrets.
**Verified core: ~80% already exists.** Findings resolved: B1 sentinel deleted
(director ruling), B2 mandatory on-device proof (field-evidenced), list-shaped
NVS schema from day one.

## A) WHAT EXISTS TODAY (reusable as-is, file:line cited)

- **Cred resolution:** `determineCredentialSource` (WiFiManager.cpp:19,100,131,
  146,344; enum `CredentialSource::{NONE,STORED_NVS,BAKED_IN}` WiFiManager.h:50).
  NVS store/clear/get: `storeCredentials` (WiFiManager.cpp:388), `clearCredentials`
  (409), `hasStoredCredentials` (396), `loadCredentials` (404); keys
  `WiFiConfig::NVS_WIFI_{SSID,PASS}` (WiFiManager.h:179).
- **NvsWifiCredentialStore** vanilla `storeWifiCredentials` (NvsWifiCredentialStore.h:37;
  keys `wifi/ssid`,`wifi/pass` — NvsWifiCredentialStore.cpp:13-14).
- **ATSETWIFI<ssid>,<pass>** handler EXISTS + registered (AtCommandDispatcher.h:
  215-244; .cpp:30), validates SSID 1-32 / pass 1-64, stores via `IWifiCredentialStore`.
- **Dual transport, one handler:** `handleTcpCommand`/`handleSerialCommand` →
  `handleCommand` (AtCommandDispatcher.h:100-103; .cpp:35-40). .ino wires both
  (can-bridge.ino:459,651). DRY satisfied.
- **TCP AUTH gate airtight** (TcpServerManager.cpp:91-115): client must send
  `AUTH <token>` or is dropped; unauthenticated TCP cannot reach commands.
- **AP fallback = default-AP story:** `AP_SSID="ESP32-CAN"`, `AP_PASS="cancan12"`
  (WiFiManager.h:176-177); entered on NONE (WiFiManager.cpp:46-52) /
  `shouldFallbackToApMode` (WiFiManager.cpp:102).
- **Makefile provisioning real:** `join-wifi` sends `AUTH <token>\rATSETWIFI
  <ssid>,<pass>\r` to 192.168.4.1:3333 (Makefile:1637-1661); `join-wifi-usb`
  writes `ATSETWIFI<ssid>,<pass>\r` to USB (Makefile:1662-1674). SSID+PASS-required
  (Makefile:1640-1645,1666-1671). USB wire = `ATSETWIFI<ssid>,<pass>\r` (Makefile:1662).
- **Factory-reset clear path exists:** BOOT-button hold wipes creds via
  `FirmwareApp` as `ICredentialClear` (can-bridge.ino:540-556).

**Decoupling is the only net-new work.** Baked creds: `-DESP32_WIFI_SSID/PASS`
(Makefile:413); consumed at can-bridge.ino:266-274 → `BAKED_SSID/BAKED_PASS`
(can-bridge.ino:317-318, `nullptr` path exists). `FIRMWARE_CRED_SENTINEL`/
`.cred-hash` (Makefile:437-447) = fragility to DELETE. `TCP_AUTH_TOKEN` baked at
Makefile:426.

## B) TARGET DESIGN (SOLID/DRY; vanilla core + thin Arduino adapters)

**B0 — MANDATORY NVS-persistence verification FIRST (see C1).** Do not remove
baking until confirmed.

**B1 — DELETE sentinel; token de-baked (director ruling).** Remove
`FIRMWARE_CRED_SENTINEL`/`.cred-hash` entirely (Makefile:437-447) — its ONLY
purpose was baked-cred freshness; KISS removes the fragility. **Option A:**
`TCP_AUTH_TOKEN` also de-baked via new `ATSETTOKEN<tok>` over the same dual-
transport dispatcher, stored in NVS with `vehicle-sim-2026` first-boot fallback.
Option B (token stays `-DTCP_AUTH_TOKEN`, "change→make clean") is smaller but
keeps a baked secret and reintroduces the silent-stale risk the sentinel guarded
for local dev. Under A the non-default-token warning (D) is mandatory.

**B2 — LIST-SHAPED NVS SCHEMA FROM DAY ONE.** Storage layer
(`NvsWifiCredentialStore` / `CredentialsService` seam) stores a LIST from day
one: `wifi/cred_count` (int, =1) + indexed entries `wifi/ssid_0`, `wifi/pass_0`
(simplest debuggable layout: one integer count + indexed string keys, human-
readable via the `prefs` CLI; avoids packed blobs). **Exposed interface stays
single-credential:** `ATSET`? no — `ATSETWIFI` writes entry[0] only; load /
`determineCredentialSource` reads entry[0] only; `cred_count` stays 1. **NO
connection-code changes:** `WiFiManager` keeps reading "the one cred" exactly as
today — no list walk/rotation/ordering/per-network retry/UI (all deferred). Value:
phase-2 multi-network is purely additive (list commands + join rotation) with
zero schema migration.

**Firmware:** no new manager. Optional `CredentialsService(INvsWifiStore&,
IWiFi&)` wrapper over set/get/clear. **Apply/revert policy:** on set, attempt join
via `ConnectingStateHandler`; on failure KEEP stored NVS creds + fall back to AP
(`shouldFallbackToApMode`), no erase on transient. Add `ATCLEARWIFI` (reuse
BOOT-button) + `ATSETTOKEN` (A).

**Makefile:** drop `-DESP32_WIFI_SSID/PASS` (Makefile:413) + token bake
(Makefile:426) + sentinel (437-447); `nullptr` baked creds (can-bridge.ino:317-318);
add/rename `make set-wifi-creds` (alias `join-wifi`, USB then AP); update header
(Makefile:396-398). NVS survives unless `ESP32_RESET_NVS=1` (subject to C1).

## C) TDD SEQUENCE (red must compile; verify `make firmware-host-tests`)

- **C1 (NEW, MANDATORY per B2):** on-device NVS-persistence proof. Flash →
  ATSETWIFI → reboot → join; then `make flash` AGAIN with NO creds → confirm
  `[STATE] wifi=WIFI_CONNECTED` without re-provision. **Field evidence:** the user
  reports creds PERSISTED across repeated re-flashes in practice — they had to
  MANUALLY erase NVS to switch SSIDs when creds were baked. This now supports the
  risk assessment empirically (verify-then-trust: C1 still mandatory) AND validates
  the UX — provision once, re-flash freely; the manual-NVS-clear pain is exactly
  what this feature removes. If C1 shows wipe, flash-workflow change is required
  before baking removal.
- **C2/C3 ALREADY WRITTEN:** ATSETWIFI at AtCommandDispatcher_test.cpp:184-213+;
  TCP unauthorized + post-auth at TcpServerManager_test.cpp:135-271.
- **C4 (NEW):** LIST-SCHEMA degenerate pin — store-then-load round-trips
  entry[0]; `cred_count`==1; entries beyond [0] never written by current interface;
  assert on-disk key shape (`cred_count`,`ssid_0`,`pass_0`) so phase-2 can't
  silently change it.
- **C5 (NEW, opt):** serial-vs-TCP equivalence; set/get/clear; apply-revert-on-
  bad-creds leaves NVS unchanged.
- **C6 (NEW):** `ATCLEARWIFI` → `clearCredentials` succeeds.
- **C7 (NEW, if A):** `ATSETTOKEN` store + first-boot default fallback.
- **C8 (NEW):** make-target smoke — CLI `--dry-run` printing exact wire bytes (TCP
  `AUTH <token>\rATSETWIFI<ssid>,<pass>\r[;ATSETTOKEN<tok>]`; USB `ATSETWIFI
  <ssid>,<pass>\r`). SSID/PASS-required without a device.

## D) SECURITY

- Creds-over-network ride the **existing AUTH-gated 3333 channel only**
  (TcpServerManager.cpp:91-115); never plaintext-unauthenticated.
- **Defaults weak — call out:** `TCP_AUTH_TOKEN` defaults to `vehicle-sim-2026`
  (Makefile:425); AP pass `cancan12` is public (WiFiManager.h:177). Over-AP
  security rests ENTIRELY on a NON-DEFAULT token. `set-wifi-creds`/network path
  MUST require or strongly WARN on the default; under A, provisioning sets a
  non-default token.
- **No secrets in logs:** PASS never printed; `show_wifi` masking (Makefile:68-80).
- **USB trust:** physical possession = authorized; no AUTH on serial.
- **Clear/factory-reset:** `ATCLEARWIFI`/BOOT-button (can-bridge.ino:540-556)
  AUTH-gated; wipes NVS → AP bootstrap.
- **Forward-compat:** encrypted transport reuses same commands over secured channel.

## E) SONAR ZERO + GATE

- New tests C1,C4-C8; `make firmware-host-tests` green.
- `make sonar-scan` + `make sonar-scan-esp32` → zero new issues.
- iOS gate: keep CLI in `scripts/`; register any new xcodeproj source (c80dfda).
- COMMIT GATE: FULL build green — `make test` + `make firmware` + `make ios` +
  bridge + submodules, ALL tests pass, zero new Sonar. Never single-component.

## F) RISKS

- **B2 NVS wipe (highest):** merged-image flash at 0x0 (Makefile:492) may pad
  0x9000 NVS with 0xFF; the field evidence above argues AGAINST a wipe, but C1
  stays mandatory. If wipe confirmed, baking removal blocked until flash fix
  (app-only image or documented re-provision).
- **Token de-bake (A):** first-boot default insecure until provisioned — D warning.
- NVS corruption: `storeWifiCredentials` checks both writes (NvsWifiCredentialStore.h:33).
- Length limits enforced (AtCommandDispatcher.h:232-237).
- Concurrent set while streaming: single-threaded loop — low risk.
- Keep AP misconfig fallback (WiFiManager.cpp:102,344). Makefile rebuild still
  works after sentinel removal (Makefile:449-459).
- Docs: README.md:73-74,149-150,177 + docs/road-test-instructions.md:96-107.

## G) ESTIMATE

- New tests: C1(on-device), C4(schema), C5(opt), C6, C7(A), C8 ~ 18-28 cases
  (C2/C3 written).
- Code: mostly deletion. Makefile (drop :413,:426,:437-447; alias); can-bridge.ino
  (nullptr baked :317-318); CredentialsService (~50 LOC) + `ATCLEARWIFI` +
  `ATSETTOKEN` (~30 LOC each); list-schema NVS keys.
- Steps: ~8 (C1 verify → delete sentinel → de-bake token → alias target → opt
  service → clear+token cmds → docs → sonar/gate).

## H) FUTURE PHASE: MULTI-NETWORK LIST (additive only)

Phase 2 adds on top of the day-one list schema with **zero migration**: list
commands (`ATSETWIFI` writes entry[i]; `ATLISTWIFI`/`ATDELWIFI`), ordering/priority
field, join rotation (try entry[0..n]), and per-network dwell/timeout budgets.
`WiFiManager` then walks the list; today's single-cred read is the i=0 degenerate
case. No schema change, no connection-semantics rework now — recorded so the
analysis isn't rediscovered.

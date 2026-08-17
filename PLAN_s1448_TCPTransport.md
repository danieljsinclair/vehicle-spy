# Plan: cpp:S1448 on TCPTransport (feat/wifi-coldboot-observability) — TDD-first, no refactor without coverage

> Planning only. No files edited. Token/password omitted. Verified against live tree 2026-08-17.

## A) INVENTORY

Source read: `include/vehicle-sim/pipeline/TCPTransport.h` (full), `src/pipeline/TCPTransport.cpp` (full).
**S1448 is LIVE:** `build-sonar/sonar-report.json` holds exactly 1 OPEN issue — cpp:S1448 "Class has 36 methods > 35" at `TCPTransport.h:135`. Count = 30 member fns + ctor + dtor + 4 deleted copy/move ops = 36. Threshold 35. Precedent: FirmwareApp.h fixed same rule by consolidating getters.

**Responsibility groups (cohesion candidates):**
1. Connection lifecycle: `open`(178)/`isOpen`(179)/`connectAndAuth`(249)/`connectUntilUp`(257)/`closeConnection`(250).
2. Handshake/HELO: `sendHeloAndParseAck`(198)/`performHeloHandshake`(258)/`readLineSkippingFrames`(213)/`sendElm327Init`(246)/`perCommandDelayMs`(248)/`sendAll`(245).
3. Hunting/resilience: `enterHuntingState`(268)+6 hunt helpers(275-309) — host-only (`#if !BUILD_IOS`, .h:267/311).
4. IO+framing: `nextLine`(180)/`takeBufferedLine`(359)/`selectReady`(364)/`readSocketIntoPending`(370)/`shouldStop`(374)/`canRead`(353)/`handleReadFailure`(389)/`formatDisconnectMessage`(383).
5. Observability: `performPing`(218)/`requestStop`(189)/`resetStop`(191)/`getDeviceId`(204).

## B) COVERAGE AUDIT — REAL GAPS ONLY (most earlier-claimed gaps are ALREADY covered)

ALREADY COVERED (do NOT re-characterize — wasted commits): auth-reject/recv-timeout/connect-failed/HELO (`TCPTransportDiagnostics.test.cpp:13-50`); `performPing` timeout (`TCPTransportLatencyResilience.test.cpp:147-163`); `sendElm327` init + failure (`TCPTransport.test.cpp`); `connectUntilUp` budget-exhaust via FakeClock instant (`TCPTransportHeloFrames.test.cpp:120-152`).

**Only FOUR real gaps remain:**
- (a) `requestStop` mid-first-connect — `connectUntilUp` (.cpp:716) stop-check not asserted.
- (b) `perCommandDelayMs` override — `atInitDelayMs_ >= 0` branch (.cpp:166-171) unpinned.
- (c) `performPing` wrong-PONG / seq-mismatch -> `-1` (.cpp:662-694) only happy path + timeout covered.
- (d) leading-`>` prompt-strip in `readLineSkippingFrames` (.cpp:360-364) not pinned directly.

## C) TARGET DESIGN (clear S1448, minimal risk)

Keep `TCPTransport` public API + `ISocket`/`IClock`/`StopToken` seams unchanged. Extract collaborators behind the same API (constructed in ctor, DI via existing seams):
- `HandshakeSequencer` (~6): owns `readLineSkippingFrames`/`performHeloHandshake`/`sendHeloAndParseAck`/`sendElm327Init`/`perCommandDelayMs`/`sendAll`.
- `ReconnectHunter` (~7): the 6 hunt helpers + `enterHuntingState` — **carries the `#if !BUILD_IOS` guards** (.h:311-325, .cpp:410/655/808) so the new TU never drags `UDPDiscovery` into iOS.
- `TcpReader` (~7): `nextLine`/`takeBufferedLine`/`selectReady`/`readSocketIntoPending`/`canRead`/`shouldStop`/`handleReadFailure`.
- `TCPTransport` -> ~14-method orchestrator (open/isOpen/connectAndAuth/connectUntilUp/closeConnection/performPing/sendHeloAndParseAck/getDeviceId/requestStop/resetStop + thin delegates). **~14 < 35 → S1448 cleared**; callers/tests untouched.

## D) COUPLINGS TO SETTLE (design decisions)

- **pending_ has THREE consumers** (`nextLine` .cpp:741-772, `readLineSkippingFrames` .cpp:338/350, `performPing` .cpp:673-684 reused buffer) and `performHeloHandshake` MUTATES it post-ACK (.cpp:401-406). → `TcpReader` owns `pending_`; `HandshakeSequencer` receives the reader (or shared buffer ref) — do NOT split the buffer.
- **connectAndAuth spans lifecycle + handshake** (AUTH .cpp:229 + elm327 .cpp:244 + HELO .cpp:247 in one body). DECISION: keep `connectAndAuth` as `TCPTransport`'s glue (orchestrator) calling `ReconnectHunter.connect` + `HandshakeSequencer`; do NOT split its body across classes.
- **Dependency cycle:** `nextLine`→`handleReadFailure`→`enterHuntingState`→`connectAndAuth`→handshake/reader; reader & hunter need each other. BREAK by having `TCPTransport` wire both and pass a `ReconnectPolicy` callback to `handleReadFailure` (strategy), not a direct hunter call.
- **host_ mutated by `finalizeHunt` (.cpp:575)** but read by `connectAndAuth`/`formatDisconnectMessage`. SETTLE ownership: `ReconnectHunter` RETURNS the resolved host (out-param / result struct), `TCPTransport` assigns — hunter never mutates `host_`.
- **performPing uses `std::chrono::steady_clock` directly** (.cpp:665/673/681), NOT `clock_`. PRE-STEP: route `performPing` through `IClock` (test: pin timeout under FakeClock) — then it may stay on `TCPTransport` or move to `TcpReader`; NOT extracted until `clock_`-routed. `sendAll` is shared by `performPing`(.cpp:666) and handshake — keep on a shared low-level seam.

## E) TDD SEQUENCE (only the 4 real gaps; full suite green each step)

1. NEW `TCPTransportConnectUntilUpStop.test.cpp` — pin `requestStop` aborts mid-first-connect (gap a). No extract (already on orchestrator). Verify `make test`.
2. NEW `TCPTransportElmDelay.test.cpp` — pin `perCommandDelayMs` override (gap b). No extract. Verify `make test`.
3. NEW `TCPTransportPingMismatch.test.cpp` — pin wrong-PONG/seq -> `-1` (gap c). First route `performPing` through `IClock`; test timeout under FakeClock. Verify `make test`.
4. NEW `FrameLineReaderStrip.test.cpp` — pin leading-`>` strip + CAN-frame discard in `readLineSkippingFrames` (gap d), red→green on CURRENT code. THEN extract `HandshakeSequencer`+`TcpReader`. Verify `make test` + `make ios-test` + `make sonar-scan` → S1448 CLEARED, 0 new.

## F) RISKS, ROLLBACK & GATE

- iOS: consumers = ctor + `open()`(.mm:413/420) + `nextLine`(.mm:119). `open()` STAYS on `TCPTransport` → conclusion holds. `ReconnectHunter` TU carries `#if !BUILD_IOS` so `UDPDiscovery` stays host-only.
- **BUILD REGISTRATION GATE (missed earlier, breaks iOS build):** every new collaborator `.cpp`/`.h` MUST be added to `test/CMakeLists.txt` AND `vehicle-sim-ios/VehicleSim/VehicleSimApp.xcodeproj/project.pbxproj` — precedent commit c80dfda registered `ReplayPacing.cpp`+`PacedFrameScheduler.cpp`. Missing = commit-gate iOS build fail.
- Abort: any `make test`/`make ios-test` regress, or `make sonar-scan` >0 new → revert extract step only.

## G) ESTIMATE

~4 new characterization tests (one per gap). 4–5 refactor commits (director commits; workers don't). Worker model: sonnet-class stepfun for test-authoring + extraction. Brief: ~110 lines.

RESULT s1448plan done

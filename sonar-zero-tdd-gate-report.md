# Sonar-Zero Coverage-Gap TDD-Gate Report

**Author:** Test Architect (Opus)
**Date:** 2026-06-30
**Branch:** `sonar_fixes`
**Baseline:** 873 tests green.

This report assesses the S3776 (cognitive complexity) and S134 (control-flow nesting)
refactor targets, highest-risk first. Every claim below is grounded in source + test
files actually read. It does **not** recommend tests for targets already locked:

- `nextLine()` framing on `TCPTransport` — **DONE**, locked by `TCPTransportNextLineContract.*`
  (see target 5 for the one flagged-flaky case).
- `DBCFileParser::parseString` S1751 — **DONE** (the loop is now a single-break `parseOneValueEntry` helper).
- `feedVINResponse` — locked by existing contract tests.
- S995 `runWithProtocol` mutation point — locked by
  `VehicleConfigResolver.test.cpp::ResolveValidVehicle_LoadsDBCIntoService`. **No more tests recommended there.**

Each target ends with exactly one verdict: **SAFE TO REFACTOR / NEEDS NEW TESTS FIRST / NEEDS TEST INFRASTRUCTURE FIRST**.

---

## 1. DBCFileParser::parseString (src/domain/DBCFileParser.cpp — S3776 / S134)

### Contract
`parseString` reads a DBC text buffer and produces a `DBCParseResult`: it recognises
`BO_` message headers (establishing `currentCanId`), `SG_` signal definitions attached
to the current message, and `VAL_` value tables, attaching each table to the matching
signal by (canId, name). Malformed lines are skipped silently; any exception yields an
empty result. In the current source, the bulk of the parsing logic has already been
extracted into private helpers (`parseSignalDefinition`, `parseOneValueEntry`,
`parseValueEntries`, `buildResult`), so the top-level `parseString` loop itself is now
low-complexity — the residual S134/S3776 pressure lives in those helpers, chiefly
`parseSignalDefinition` (manual cursor walk) and the `VAL_` arm.

### Already locked by existing tests
- `DBCFileParser.test.cpp`: empty/whitespace/garbage inputs, message-with-no-signals,
  single & multiple signals, all four byte-order/signedness combinations, non-zero offset,
  large scale, two value-table sizes, comment/noise-line skipping, real Tesla fragment,
  zero min/max.
- `DBCSignalMapperContract.test.cpp` (`DBCFileParserContractTest`): empty unit string,
  cross-byte-boundary startBit preserved, multiple value tables attached to correct
  signals by name, value table referencing an unknown signal is ignored, signals across
  blank lines preserve order, full parse is consumable by the mapper end-to-end.
- `DBCParser.test.cpp` (42 tests): signal-definition construction/equality,
  `DBCParseResult` empty/unknown-id, and bit-extraction contracts.

### Untested branches/behaviours a refactor could silently break
1. **Multiplexor parsing** (`parseSignalDefinition` lines 52–60): the `M`/`m<n>` branch
   that consumes an optional multiplexor indicator before `:` is exercised by **no** test
   — every fixture signal is unmultiplexed. A refactor could drop/alter it silently.
2. **`SG_` before any `BO_`** (`parseString` line 275 `currentCanId == 0` continue): no
   fixture places an `SG_` ahead of its message. Locking it prevents a refactor from
   attaching orphan signals to canId 0.
3. **Malformed `SG_` line is dropped** (line 278 `parseSignalDefinition` returns false):
   no test feeds a structurally broken signal (missing `:`, missing `|`, non-numeric
   startBit/bitLength, missing `@`, missing `(` `[`). `parseSignalDefinition` has ~12
   early-return failure paths; none are individually locked.
4. **`VAL_` with malformed entries** (`parseOneValueEntry` returning false → break):
   only well-formed tables are tested. A trailing junk token or an unquoted label is
   untested.
5. **`BO_` with non-numeric / overflow id** (line 271 `from_chars` failure): untested.
6. **`parseFile` / `canParse` happy path** (file open + read): only
   `CanParseReturnsFalseForNonexistentFile` exists; the *success* path of `parseFile`
   and `canParse` (file exists, non-empty first line → true) is not directly tested
   without a transient file. (Existing happy-path coverage comes via `parseString`.)

### Verdict
**NEEDS NEW TESTS FIRST.** The happy paths are thoroughly locked, but the **error/edge
arms of `parseSignalDefinition` and the multiplexor arm are blind** — exactly the
branches a "reduce cognitive complexity" refactor would touch. Decomposing that cursor
walk is the whole point of the ticket; doing it blind risks silently accepting malformed
input or dropping the multiplexor.

### Blind test contracts (behaviours, not implementation)
1. Given a well-formed `SG_` line carrying an `M` multiplexor marker, `parseString`
   yields the signal with its name, startBit, bitLength, and byte-order intact (the
   multiplexor text is consumed and does not corrupt the parse).
2. Given an `SG_` line appearing before any `BO_` message header, `parseString` yields
   zero signals.
3. Given an `SG_` line missing the `:` separator after the name, `parseString` yields
   zero signals (the line is dropped, not partially parsed).
4. Given an `SG_` line whose `startBit|bitLength` field is non-numeric, `parseString`
   yields zero signals.
5. Given an `SG_` line missing the `(...)` scale/offset group, `parseString` yields
   zero signals.
6. Given a `VAL_` line whose final entry is unquoted/malformed, `parseString` parses the
   preceding well-formed entries and attaches them to the matching signal (or yields the
   signal with an empty table if none parse).
7. Given a `BO_` header whose numeric id is non-numeric, `parseString` yields zero
   signals and does not crash.
8. Given a `VAL_` line whose numeric id is non-numeric, `parseString` ignores it without
   attaching a table.

### Coverage threshold to proceed
All 8 above pass; the existing Tesla-fragment and multi-value-table tests stay green.
Refactor of `parseSignalDefinition` into named sub-parsers is then safe.

---

## 2. ELM327Transport::parseOBD2Response / parseCANFrame (src/boundary/ELM327Transport.cpp — S3776 cc37/cc29 + S134)

### Contract
`parseOBD2Response` converts an ASCII ELM327 OBD2 reply into a binary byte vector:
strips the `>` prompt, strips informational prefixes (`SEARCHING`, `BUSINIT`, `OK`),
rejects error strings, then accumulates hex digits into bytes. `:` resets the hex
accumulator (line-number stripping for multi-frame). An odd trailing hex digit or an
invalid hex pair yields nullopt. `parseCANFrame` classifies a single monitor line: it
rejects prompts, known ELM327 status words, and OBD2 multi-frame (`:` present), then
accepts only 9–10 whitespace-separated hex tokens representing CAN-id + 8 data bytes
(with an optional `0x600`-range type prefix).

### Already locked by existing tests
- `ELM327Protocol.test.cpp`: RPM/speed/throttle happy parse, `NO DATA` (incl.
  case-insensitive), `SEARCHING...` prefix strip, `UNABLE TO CONNECT`, `BUS ERROR`,
  multi-line VIN parse (line-number stripping via `:`), AT-command building.
- `ELM327Transport.test.cpp`: `parseCANFrame` valid frame with/without type prefix,
  prompt, prompt+CR, empty, OBD2 response rejection, invalid hex, line-number
  rejection, `OK`/`BUS INIT` rejection, CRLF, multi-space, lowercase hex, extended
  CAN id; CAN-monitor init sequence (size, delays, ends in `ATMA`), `buildCANFilter`.

### Untested branches/behaviours a refactor could silently break
1. **Odd trailing hex digit → nullopt** (`parseOBD2Response` line 133): untested — a
   refactor could relax to truncation.
2. **Invalid hex pair mid-stream → nullopt** (line 115): the `XX` rejection is locked
  only for `parseCANFrame`, not for `parseOBD2Response`.
3. **`BUSINIT`/`OK` prefix strip** in `parseOBD2Response` (INFO_MESSAGES, lines 76–90):
   only `SEARCHING` is fixture-covered; `BUSINIT` and `OK` prefix-stripping paths are
   not.
4. **`:` mid-stream reset** in `parseOBD2Response`: exercised only transitively via the
   VIN test; no direct contract that a `:` discards accumulated digits.
5. **`parseCANFrame` 8-data-byte exactness** (line 305 `dataCount != 8`): no fixture
   asserts that 7 or 9 data bytes are rejected. A refactor could widen the count.
6. **Type-prefix range boundary** (`0x600`–`0x6FF`, lines 289–294): only `0x610` is
   tested; a token just outside the range (e.g. `0x5FF` or `0x700`) is not, nor is the
   "10 tokens but first token not a type prefix" rejection.
7. **Empty prompt-only / `>`-only response** in `parseOBD2Response` (line 73): not
   directly asserted.

### Verdict
**NEEDS NEW TESTS FIRST.** `parseOBD2Response` is the highest-cc function in the file
and is consumed by **two** production callers (`OBD2Protocol.cpp`,
`BLEManagerBase.cpp`). Its error/rejection arms (odd digits, invalid hex mid-stream,
non-`SEARCHING` info prefixes) are largely blind, and those are precisely the
behaviours a complexity refactor must not change.

### Blind test contracts (behaviours, not implementation)
1. Given an OBD2 response whose hex content has an odd number of digits, `parseOBD2Response`
   returns nullopt.
2. Given an OBD2 response containing an invalid hex pair mid-stream, `parseOBD2Response`
   returns nullopt.
3. Given an OBD2 response prefixed with `BUSINIT...`, `parseOBD2Response` strips the
   prefix and parses the remaining hex when well-formed.
4. Given an OBD2 response containing a `:` line-number segment, the digits accumulated
   before the `:` are discarded and only post-`:` hex is returned.
5. Given a response that is only the `>` prompt (or empty after prompt strip),
   `parseOBD2Response` returns nullopt.
6. Given a CAN monitor line with seven or nine data bytes (not eight),
   `parseCANFrame` returns nullopt.
7. Given a 10-token CAN line whose first token is outside the `0x600`–`0x6FF` type range,
   `parseCANFrame` returns nullopt.

### Coverage threshold to proceed
All 7 pass; the existing RPM/speed/throttle/VIN and CAN-frame happy paths stay green.
The accumulator loop can then be decomposed safely.

---

## 3. DBCSignalMapper::mapSignal / mapGearSignal / extractRawBits (src/domain/DBCSignalMapper.cpp — S3776 cc31 + S134)

### Contract
`mapSignal` extracts raw bits from a CAN frame per a `DBCSignalDefinition` (Intel
sequential, or Motorola sawtooth), applies scale/offset, clamps to [min,max], and
returns nullopt if the signal's last bit falls outside the frame. `mapGearSignal`
specialises: it looks up the definition by (canId, name), and for raw values 0/7
returns nullopt; otherwise maps the value-table description to a canonical `Gear`.
`extractRawBits` is the bit-level core (Intel little-endian, Motorola big-endian
sawtooth, verified against cantools).

### Already locked by existing tests
- `DBCSignalMapperContract.test.cpp`: full gear mapping (P/R/N/D and INVALID/SNA →
  nullopt), unknown canId, unknown signal name, empty-definitions lookup nullopt,
  signed Motorola crossing a byte boundary, parse→mapper end-to-end.
- `DBCParser.test.cpp` (`DBCSignalMapperTest`, `DBCBitExtractionTest`): DIR_axleSpeed
  known/zero/negative, torque actual known/zero/negative-regen, accel pedal
  known/full/zero, steering angle ±, frame-too-short nullopt, 1-bit boolean, Motorola
  8/4/16-bit spanning, mapSignal by canId+name, Intel 3-bit at bit 21 (all values),
  Motorola 4-bit at bit 55, Motorola 14-bit crossing boundary, Motorola 4-bit at bit 23.

### Untested branches/behaviours a refactor could silently break
1. **`extractRawBits` bitLength guard** (line 112 `bitLength == 0 || > 64` → return 0):
   no fixture passes a 0 or >64 bitLength directly. The clamp branch in `mapSignal`
   then yields offset, not an error.
2. **Clamp boundary** (line 22/25 `std::clamp`): no fixture asserts that a raw value
   yielding a physical result outside [min,max] is clamped to the bound rather than
   returned raw. This is a real behavioural contract a refactor must preserve.
3. **`mapGearSignal` with empty value table** (lines 62–69): the comment explicitly
   says "this shouldn't happen for DI_gear" and falls back to direct raw mapping. That
   fallback branch is untested.
4. **`mapGearSignal` unknown raw value (not 0/7, not in table)** (line 102 nullopt):
   not directly tested (all fixtures use a table-defined value or 0/7).

### Verdict
**NEEDS NEW TESTS FIRST.** The bit-extraction core is the most heavily tested code in
this list and is safe, but the **clamp-to-[min,max]** behaviour and the
**gear empty-table fallback** are untested contracts that a complexity refactor of the
clamp/branch logic could silently alter.

### Blind test contracts (behaviours, not implementation)
1. Given a signal definition whose physical value (raw × scale + offset) exceeds `max`,
   `mapSignal` returns `max` exactly (clamped, not the over-range raw).
2. Given a signal definition whose physical value is below `min`, `mapSignal` returns
   `min` exactly.
3. Given a gear signal definition with an empty value table and a non-zero, non-7 raw
   value, `mapGearSignal` returns that raw value cast to int32.
4. Given a gear signal whose raw value is in the table range but matches no table entry
   (and is not 0 or 7), `mapGearSignal` returns nullopt.

### Coverage threshold to proceed
All 4 pass; existing bit-extraction and gear-mapping suites stay green.

---

## 4. SecureTcpTransport::open / readEncryptedLine / nextLine (src/pipeline/SecureTcpTransport.cpp — S3776 cc28 + S134)

### Contract
`open()` connects to host:port with retry/backoff, returning false immediately on
connection-refused (`-2`) or handshake/auth failure, and only retrying on transient
connect failures. `readEncryptedLine` peels length-prefixed XChaCha20-Poly1305 frames
from a socket buffer, decrypting each with the session rx key; a tampered/undecryptable
frame or a disconnect sets `exhausted_` and returns nullopt. `nextLine` reassembles
plaintext lines (newline-terminated) from decrypted frames and flushes a trailing
partial line on EOF.

### Already locked by existing tests
- `SecureTcpTransport.test.cpp` (real loopback + libsodium mock server): handshake
  success with correct key, handshake failure with wrong key, encrypted round-trip of
  3 lines, connection refused, clean disconnect → nullopt, `requestStop` terminates
  `nextLine` promptly, 10-line sequence drained in order.

### Untested branches/behaviours a refactor could silently break
1. **Backoff/retry on transient failure** (`open` lines 239–250): no fixture induces a
   transient `connectToHost < 0` that then succeeds. The backoff counter and delay
   calculation (`calculateReconnectDelayMs`) are untested.
2. **Raw-buffer overflow guard** (line 360 `MAX_LINE_LEN`): untested; a refactor could
   drop the cap.
3. **Tamper-detection sets exhausted** (line 309): only covered by the wrong-key path at
   the handshake; the post-handshake tampered-ciphertext path (decryption returns
   non-zero) is not directly exercised.
4. **Coalesced-frame peeling** (line 289 onward): the 10-line test exercises multiple
   frames, but a single recv() carrying parts of two frames (split mid-frame) is not
   explicitly locked — though length-prefixing makes it robust.
5. **Trailing partial line flush on EOF** (`nextLine` lines 384–388): every test sends
   newline-terminated lines, so the "last line without terminator is still returned"
   branch is untested.
6. **`select` EINTR continue** (line 339): not testable deterministically; document, do
   not test.

### Verdict
**NEEDS NEW TESTS FIRST** (mild). The core security path is well locked, but the
retry/backoff arithmetic, the overflow cap, and the partial-line-on-EOF behaviour are
the realistic refactor hazards. All can be covered with the existing loopback
infrastructure — no new test infra needed.

### Blind test contracts (behaviours, not implementation)
1. Given a server that refuses one connect then accepts the next, `open()` eventually
   succeeds (transient-failure retry).
2. Given a server that sends a single encrypted frame split across two TCP segments,
   `nextLine()` reassembles and returns the correct plaintext line.
3. Given an encrypted frame whose ciphertext/tag has been tampered after the handshake,
   `nextLine()` returns nullopt and the transport becomes not-open.
4. Given a server that sends a final plaintext line without a trailing newline then
   closes, `nextLine()` returns that line on one call and nullopt on the next.
5. Given a peer that floods bytes beyond `MAX_LINE_LEN`, `nextLine()` returns nullopt
   rather than growing the buffer without bound.

### Coverage threshold to proceed
All 5 pass; the 7 existing handshake/round-trip tests stay green.

---

## 5. TCPTransport::nextLine residual (src/pipeline/TCPTransport.cpp — S3776 cc35 after nextLine refactor)

### Contract
`nextLine()` returns complete transport lines from the socket, framing on `\r` or `\n`
(each independently), buffering partial lines across recv() calls, returning nullopt on
stop-request / clean EOF, and re-entering hunting/reconnect on peer disconnect.

### Already locked by existing tests
- `TCPTransportNextLineContract.*` (11 tests): not-open / open-failed guards, `\r`, `\n`,
  `\r\n`, `\r\r` banner empty line, multi-line drain, partial-line across two reads,
  >256-byte line assembly, buffered fast path, clean EOF line-then-nullopt.
- Plus the broader `TCPTransportTest` suite: raw auth, frame parsing through the
  normaliser, clean disconnect, connection refused, ELM327 init, init failure,
  `requestStop` promptness.

### Untested branches/behaviours a refactor could silently break
1. **`selectReady` EINTR continue** (line 595): not deterministically testable.
2. **`MAX_PENDING_LEN` overflow cap** (`readSocketIntoPending` line 570): untested; a
   refactor could drop the defensive clear.
3. **Hunt-on-disconnect happy path** (lines 628–641, non-iOS): the reconnect-via-hunt
   path is not directly fixture-covered (only the stop-guarded `exhausted_` exit is).
   This is significant residual complexity.

### Verdict
**NEEDS TEST INFRASTRUCTURE FIRST** for the hunting path; the framing path itself is
**SAFE TO REFACTOR**. The framing loop (the actual S3776 target after the `takeBufferedLine`
extraction) is thoroughly locked by `TCPTransportNextLineContract`. The remaining cc35
is dominated by the `#if !iOS` hunting/reconnect block, which needs a controllable
disconnect-then-reaccept loopback harness to test safely — beyond what the current
`LoopbackServer` provides.

### Coverage threshold to proceed
- Framing-loop decomposition: **proceed now** — contract coverage is sufficient.
- Hunting/reconnect decomposition: gate on a new test infra that can simulate a peer
  that disconnects mid-stream and re-accepts, then add: (a) given a disconnect during a
  read, `nextLine()` reconnects and resumes returning subsequent lines; (b) given a
  disconnect that cannot be recovered, `nextLine()` returns nullopt.

---

## 6. DBCFileParser other S134 nesting sites

### Contract
Beyond `parseString`, the nested cursor walks in `parseSignalDefinition` (the `M`/`m`
multiplexor loop, the bracket/paren scanning) and `parseValueEntries`/`parseOneValueEntry`
carry residual S134 nesting.

### Already locked / gaps
Covered by the same tests as target 1. The specific untested nesting is the
multiplexor branch and the malformed-entry early-returns — already enumerated in target 1's
blind contracts (items 1, 3–6, 8). No additional gaps beyond target 1.

### Verdict
**NEEDS NEW TESTS FIRST** — satisfied by target 1's blind contracts. Do not duplicate.

---

## 7. VehicleSignalFactory (src/domain/VehicleSignalFactory.cpp — S134)

### Contract
At construction, `resolveMappings()` joins each `config_.signalMappings` entry
(signalName → fieldName) to its DBC definition, classifying it Numeric, Gear, or
Ignored (field not emitted, or no DBC definition). `build()` then, for each resolved
mapping, looks up the one frame by canId and decodes it into the parallel numeric/gear
output slots, returning a `VehicleSignal` with unmapped fields as nullopt.

### Already locked by existing tests
- `VehicleSignalFactory.test.cpp`: single/multiple mapped signals, unmapped → nullopt,
  missing frames → nullopt, full Tesla integration, empty frames → all-nullopt default,
  negative torque, gear-defaults-nullopt, gear-4→AUTO_1, gear-0→nullopt, gear-7→nullopt,
  analog speed 500→50km/h.

### Untested branches/behaviours a refactor could silently break
1. **Ignored field path** (`resolveMappings` lines 66–70): a mapping to a field
   `VehicleSignal` does not expose (e.g. `gearRequested`) is classified Ignored and must
   neither decode nor crash. Not directly tested.
2. **Mapped signal with no DBC definition** (lines 88–91): untested — must be treated as
   Ignored (not crash, not invent a value).
3. **Numeric slot table completeness** (`numericSlotFor`): only a subset of the 9 numeric
   fields appear in fixtures (`motorRpm`, `throttlePercent`, `steeringAngleDeg`,
   `speedKmh`). `brakePercent`, `accelerationG`, `motorHvVoltage`, `motorHvCurrent`,
   `motorTorqueNm` are hit incidentally; an explicit field→slot contract test would
   catch a slot-table drift.

### Verdict
**NEEDS NEW TESTS FIRST** (small). The factory is well covered on happy paths, but the
**Ignored classification** (both reasons) and the **slot-table completeness** are the
behaviours the S134 refactor of `resolveMappings`/`build` could silently regress.

### Blind test contracts (behaviours, not implementation)
1. Given a config mapping a signal name to a field the factory does not emit (e.g.
   `gearRequested`), and a frame carrying that signal, `build()` leaves the corresponding
   output field absent (Ignored) without error.
2. Given a config mapping a signal name that has no DBC definition in the parse result,
   `build()` leaves the output field absent without error.
3. Given config mappings that cover each numeric field the factory emits, frames carrying
   each signal populate the corresponding output field in the returned `VehicleSignal`.

### Coverage threshold to proceed
All 3 pass; the existing 13 factory tests stay green.

---

## 8. PipelineReplay::runReplay (src/pipeline/PipelineReplay.cpp — S134)

### Contract
`runReplay` loops over transport lines, records each verbatim to the raw sink (if any),
normalises it, and dispatches by `NormaliserResultKind`: Frame → translate → write to
decoded sink → report progress; Skip → count; Malformed/default → count. On loop exit it
reports completion and returns aggregated `ReplayStats`.

### Already locked by existing tests
- `PipelineReplay.test.cpp`: file replay writes only CSV (never raw.txt) with correct
  stats (linesRead/malformed/skipped), raw sink records verbatim, capture-timestamp
  preservation, live-path wall-clock timestamps, null-decoded-sink tolerance.

### Untested branches/behaviours a refactor could silently break
1. **Null raw sink** (line 27 `if (rawSink)`): exercised indirectly, but no fixture
   combines null-raw-sink with non-null-decoded-sink asserting stats — minor.
2. **Null progress reporter** (lines 49, 65): not directly tested with a non-null decoded
   sink; the existing tests pass `nullptr` progress implicitly via the file path.
3. **Frame result that fails to decode** (`translationService.processFrame` returns
   nullopt): no fixture asserts that an undecodable-but-valid frame increments neither
   `framesDecoded` nor `malformedLines`. A refactor could mis-bucket it.

### Verdict
**NEEDS NEW TESTS FIRST** (small). The S134 `switch` is simple and largely locked, but
the **decode-failure bucketing** and the **null-sink/reporter combinations** are the
realistic refactor hazards. No new test infra required.

### Blind test contracts (behaviours, not implementation)
1. Given a frame-shaped normalised line whose bytes decode to no signal (e.g. unknown
   CAN id), `runReplay` counts it as neither decoded nor malformed (linesRead increments,
   framesDecoded does not).
2. Given a null progress reporter and a non-null decoded sink, `runReplay` writes the
   decoded CSV and returns correct stats without dereferencing the null reporter.
3. Given a null raw sink and a non-null decoded sink, `runReplay` writes the decoded CSV
   and does not attempt to record raw lines.

### Coverage threshold to proceed
All 3 pass; the existing 5 replay tests stay green.

---

## Cross-cutting assessments

### TCPTransportNextLineContract.CrlfFramesLineThenEmptyBannerLine — flaky?
**Verdict: pre-existing / structural loopback-timing, not a refactor hazard — but worth
hardening.** The test sends `"FRAME1\r\nFRAME2\r"` and expects three `nextLine()` calls
(`FRAME1`, `""`, `FRAME2`). Each `nextLine()` reads with a 1ms `select()` floor and a
1-byte (0/1ms) socket recv timeout (see `openConnectedTransport`). There is no
synchronisation between `sendBytes` and the client's reads: all three lines may arrive
in one recv() (then they're served from the buffer fast-path — robust), or the first
`nextLine()` may fire before `"FRAME2\r"` has been written, relying on the second/third
call to re-poll. Because framing is terminator-driven and the buffer is preserved across
calls, the *outcome* is deterministic — but the *timing* is not, and under load a
`nextLine()` can momentarily find no complete line and re-enter the `select` loop. This
is the same class of timing the `PartialLineAcrossTwoReadsAssembles` test already
acknowledges with an explicit `sleep_for(20ms)`. It is **not** caused by the recent
refactor and is **not** a blocker for TCPTransport framing work (the framing contract is
sound). If it flakes in CI, the stability fix is a small deterministicisation (e.g. a
short sleep after `sendBytes` for the multi-line case, or asserting via the buffered
fast-path only). Recommend: track separately, do not gate the refactor on it.

### S995 runWithProtocol
**Confirmed locked** by `VehicleConfigResolver.test.cpp::ResolveValidVehicle_LoadsDBCIntoService`.
No additional tests recommended — per instructions.

---

## Summary verdicts

| # | Target | Verdict |
|---|--------|---------|
| 1 | DBCFileParser::parseString + helpers | **NEEDS NEW TESTS FIRST** (8 blind contracts) |
| 2 | ELM327Transport parseOBD2Response/parseCANFrame | **NEEDS NEW TESTS FIRST** (7 blind contracts) |
| 3 | DBCSignalMapper (clamp + gear fallback) | **NEEDS NEW TESTS FIRST** (4 blind contracts) |
| 4 | SecureTcpTransport open/readEncryptedLine/nextLine | **NEEDS NEW TESTS FIRST** (5 blind contracts) |
| 5 | TCPTransport nextLine residual | Framing **SAFE TO REFACTOR**; hunting **NEEDS TEST INFRASTRUCTURE FIRST** |
| 6 | DBCFileParser other S134 sites | Covered by target 1 — **NEEDS NEW TESTS FIRST** (same set) |
| 7 | VehicleSignalFactory | **NEEDS NEW TESTS FIRST** (3 blind contracts) |
| 8 | PipelineReplay::runReplay | **NEEDS NEW TESTS FIRST** (3 blind contracts) |

**Recommended order (highest blast-radius first):** 1 → 2 → 3 → 4 → 7 → 8, with target 5
framing able to proceed immediately and its hunting path deferred.

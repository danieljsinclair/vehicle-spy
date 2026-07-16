# TODO_vspy_posix_socket_tests.md — real, fast tests for PosixSocket (happy path + instant errors)

> Read `TODO.md` first (constraints, gate, delegation model, self-check). This closes the ONE gap the fast-tests refactor left: **`PosixSocket` (the production syscall wrapper) now has zero test coverage** — every TCPTransport test injects `FakeSocket`, which *replaces* `PosixSocket`. Add REAL loopback tests for `PosixSocket`: fast (<10ms), happy path + **only** the error paths that are ~instant. Timeout/unreachable paths stay with `FakeSocket` (they are non-deterministic by nature). Single-writer on the new test file (e.g. `test/pipeline/PosixSocket.test.cpp`); do **not** touch the existing TCPTransport tests.

## WHY
The `ISocket` seam lets TCPTransport tests run <10ms via `FakeSocket` — correct, keep that. But `FakeSocket` replaces `PosixSocket`, so `PosixSocket`'s own `connect`/`recv`/`selectReadable`/`sendAll`/`close` are exercised by nothing. `PosixSocket` is a thin wrapper (low bug-surface), but currently untested. These tests recover that coverage **without** re-introducing the old slowness — real loopback is sub-millisecond; the old 39s was unreachable-IP hangs + backoff sleeps, neither of which is needed here.

## ROLES — two test architects (the quality bar, mandatory)
1. **Author architect:** writes REAL tests against `PosixSocket` (the production class — **NOT** `FakeSocket`). Read `include/vehicle-sim/pipeline/ISocket.h` + `src/pipeline/PosixSocket.cpp` first to enumerate the contract. Write from the observable spec, not by copying an existing test's shape.
2. **Critic architect (HARSH):** reviews every test for VALUE and must reject any test that is:
   - **fragile** — depends on hard-coded ports, wall-clock timing, machine state, or test order;
   - **testing a mock** — uses `FakeSocket` or any stand-in instead of the REAL `PosixSocket` (defeats the entire purpose);
   - **tautological** — asserts the test's own setup ("I sent X, received X" with no `PosixSocket` behaviour in between), or tests the OS rather than our wrapper;
   - **low-value** — duplicates what a simpler test already proves.
   For each surviving test the critic must state **exactly which `PosixSocket` code path it exercises and why that path matters.** Tests the critic cannot justify get cut.

## WHAT TO TEST (real loopback, <10ms each)
**Happy path** (real in-process peer — a tiny accept/send thread, or a paired socket):
- `connect` → succeeds against a listening loopback socket.
- `sendAll` → bytes arrive at the peer.
- `recv` → returns the bytes the peer sent (including a natural short/partial read).
- `selectReadable` → signals readable when data is pending.
- `close` → releases the fd cleanly.

**Instant error paths — ONLY these** (they return near-instantly on loopback):
- `connect` to a closed port → `ECONNREFUSED` → returns false. (Instant on loopback.)
- `recv` after the peer closes → returns 0 (peer-close). (Instant.)

## WHAT NOT TO TEST HERE (leave to FakeSocket)
- `selectReadable` **timeout** (returns 0 after N ms) — non-deterministic duration; `FakeSocket` scripts it instantly. ✗
- `connect` to an **unreachable IP** (`127.0.0.2` hangs ~5s on macOS) — non-deterministic; `FakeSocket`. ✗
- Any path that needs a real wall-clock wait to exhibit. **If a path cannot be made ~instant, it belongs in `FakeSocket`, not here.**

## QUALITY BAR (every test)
- Uses the **REAL `PosixSocket`** (construct it directly; no `FakeSocket` injection in this file).
- **<10ms each** (report per-test runtime).
- **Deterministic:** ephemeral port (bind `127.0.0.1`, let OS choose the port), no hard-coded ports, no fixed `sleep_for`, clean up fd + peer thread in teardown (no leaks into the next test).
- Each assertion exercises a real `PosixSocket` path, not the test's own plumbing.
- No fragile timing — if a test would need `sleep_for` to avoid a race, redesign it with readiness/select or a latch.

## ACCEPTANCE
1. `PosixSocket`'s `connect`/`recv`/`selectReadable`/`sendAll`/`close` happy paths covered by real-loopback tests.
2. Instant error paths (refused, peer-close) covered; timeout/unreachable paths **not** here (`FakeSocket` owns them).
3. Every test <10ms; suite deterministic (run 3× consecutively, zero flake).
4. **Critic signed off:** no fragile / mock-testing / tautological / low-value tests survive; each surviving test's exercised path + value documented.
5. `make gate` green; vehicle-spy Sonar OPEN=0 (new test file must introduce no issues). No push. No suppression. No skipped tests.

## SELF-CHECK + REPORTING (per TODO.md)
Spawn a verifier teammate; report:
- Commit SHA(s) (`test:` prefix); end `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
- Per-test runtime (must be <10ms) + the `PosixSocket` paths covered.
- Critic's verdict: which tests rejected + why; which survived + their value.
- `make gate` + live Sonar vehicle-spy OPEN=0.
- Confirmation the new test uses real `PosixSocket` (grep the file for `FakeSocket` = 0 hits).

## DO NOT
- Don't push. Don't suppress. Don't use `FakeSocket` in this file. Don't add timeout/unreachable-path tests here. Don't touch the TCPTransport tests. Don't weaken a test to make it pass — if a path can't be made fast + deterministic, leave it to `FakeSocket` and say so.

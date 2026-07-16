# TODO_vspy_fast_tests.md — mock-based fast TCPTransport tests (<10ms, no real I/O)

> Read `TODO.md` first. This is the BIG vehicle-spy workstream. Do it AFTER `TODO_vspy_s107.md` lands (it further touches the ctor). TCPTransport is **single-writer** — no other agent edits `src/pipeline/TCPTransport.*` or the TCPTransport tests while you work.

## HARD REQUIREMENTS (user-mandated, non-negotiable)
1. Every `TCPTransport` hunting/nextLine test runs in **<100ms, ideally <10ms**. Current suite is ~39s — ~500× too slow.
2. **NO real I/O** in the unit tests: no real sockets, no real `connect`/`recv`/`select`, no real loopback server, no real `sleep`/`sleep_for`. Mock it all. (Lowering timeouts is NOT acceptable — real I/O at any timeout is still nondeterministic real I/O.)
3. **DRY: one fake-time framework.** EXTEND `include/vehicle-sim/util/IClock.h` (`IClock`/`SystemClock`/`FakeClock`). REMOVE the parallel `IBackoffSleeper`/`RealBackoffSleeper`/`InstantBackoffSleeper` seam added in commit `92365cd` — fold its job into `IClock`. No parallel fake-clock/sleeper implementations anywhere.

## Root cause (so you design the right fix)
The slowness is NOT one thing — it's real I/O across the board:
- backoff `std::this_thread::sleep_for` (1–4s) — the only part faked so far (via the seam you're removing)
- real `connect()` to an unreachable loopback IP (`127.0.0.2` hangs ~5s on macOS)
- real loopback HELO handshake (~1s of `recv` round-trips)
- the real discovery thread + its poll/join
Lowering timeouts only chips at the connect; the handshake + sleeps still dominate. **The only way to <10ms is to mock the network + the clock.**

## Approach — 4 phases, verify each before the next

### Phase 1 — `ISocket` seam (mock the network I/O)
TCPTransport currently calls POSIX `connect()`/`recv()`/`select()` directly on `fd_`. Introduce an interface so tests inject scripted I/O:

```cpp
// include/vehicle-sim/pipeline/ISocket.h
class ISocket {
public:
    virtual ~ISocket() = default;
    virtual bool connect(const std::string& host, int port) = 0;   // true=connected; false=refused/unreachable
    virtual ssize_t recv(char* buf, size_t len) = 0;               // >0 bytes; 0=peer-close; <0=error
    virtual int selectReadable(int timeoutUs) = 0;                 // >0 ready; 0 timeout; <0 error
    virtual void close() noexcept = 0;
    // add only what TCPTransport actually calls — read the .cpp first to enumerate
};
```

- **Production:** `PosixSocket` — a verbatim behavior-port of today's direct POSIX calls. **Zero behavior change** on the production path (this is critical — the backoff/reconnect timing is load-bearing).
- **Test:** `FakeSocket` — scriptable: a queue of `connect` outcomes (selectable per host, so unreachable-host fails fast + reachable-host succeeds), a script of `recv` bytes (handshake ACK + data lines + peer-close at scripted points), + `selectReadable` readiness. Instant + deterministic.
- Inject `ISocket` into `TCPTransport` (a factory `std::function<std::unique_ptr<ISocket>()>` or an instance; production default = `PosixSocket`). Route every connect/recv/select through it.

**Blind-TDD note:** the EXISTING tests (`TCPTransportHuntingGapTest` 9 tests + `TCPTransportHuntingTest` + `TCPTransportHuntingCancelTest` + the nextLine tests) are the **behavior spec**. They must stay GREEN throughout — first as they are (real I/O), then once you swap to `FakeSocket` they go green instantly. Do NOT change assertions; only swap the I/O substrate.

### Phase 2 — extend `IClock`, remove `IBackoffSleeper`
- Audit `IClock`'s current API (`include/vehicle-sim/util/IClock.h`) + its existing consumers (`grep -rn IClock firmware/ test/ include/`). Extend it to cover TCPTransport's time-waits: the backoff sleep, the select timeout, the discovery poll interval. Add a method ONLY if no existing one fits (e.g. `sleepFor(ms)`, or adapt `waitFor`). Keep the interface cohesive (SRP — it's a clock).
- Production = `SystemClock` (real time). Test = `FakeClock` (advances instantly / under test control).
- **Remove** `IBackoffSleeper`, `RealBackoffSleeper`, `InstantBackoffSleeper`, and `HuntResilienceConfig::sleeper`. `HuntResilienceConfig` then carries just `DiscoveryListenerFactory` (re-evaluate whether it still earns a struct — if it's down to one field, consider reverting to a bare ctor param; but mind S107).
- Route TCPTransport's `sleep_for` / timeouts through `IClock`.

### Phase 3 — rewrite tests to `FakeSocket` + `FakeClock`
- Every hunting/nextLine test injects `FakeSocket` (scripted I/O) + `FakeClock` (instant time). **Remove** `HuntingServer`/`LoopbackServer` from these tests (they're real loopback — the source of the real I/O). Grep the test files after: no real socket/bind/listen/accept/connect outside any genuinely-integration test (there shouldn't be one — the hard requirement is <100ms for all of these).
- Keep **every assertion identical** to the current spec. Only the I/O/time substrate changes. The 9 gap tests (G1/G2/G3/G5/G6G7/G8/G11/N1/N5) especially — G3 (8-char prefix), G8 (sub-slice cancellation), N1 (reconnect-continue) are the load-bearing ones.
- Target: each test <10ms.

### Phase 4 — keep S107 clear
- The `ISocket` + `IClock` injections add ctor params. Keep the ctor under the S107 threshold (7): the endpoint grouping from `TODO_vspy_s107.md` is already done; if needed, group the infrastructure injections (`ISocket` factory, `IClock`, `output`, `timing`, `stop`, discovery factory) into a `TransportDeps`/`TransportConfig` — but ONLY if they're genuinely a cohesive "transport construction" domain object (not a band-aid). Verify the final ctor param count stays under threshold + S107 stays closed.

## Acceptance (ALL must hold — the verifier checks each)
1. **Per-test runtime <100ms** (report the max + the total; target <10ms each). Before was ~39s total.
2. **Zero real I/O** in the TCPTransport unit tests — grep confirms no `HuntingServer`/`LoopbackServer`/real `connect`/real port binding.
3. **All host tests green; assertions UNCHANGED** from the current spec (`git diff` on the tests shows only I/O-substrate swaps, not assertion changes).
4. **DRY:** `IBackoffSleeper`/`RealBackoffSleeper`/`InstantBackoffSleeper` gone (grep = 0 refs); `IClock` is the sole fake-time path.
5. **vehicle-spy Sonar OPEN = 0** (live API); full `make gate` green (incl. ios + ios-analyze — TCPTransport is compiled into the iOS app).
6. **Production behavior identical:** the `PosixSocket` + `SystemClock` path reproduces today's exact behavior. The characterization tests (which assert real behavior) prove this by staying green.
7. **Commits logically separated** (e.g. `refactor: inject ISocket seam into TCPTransport`; `refactor: extend IClock, remove IBackoffSleeper`; `test: fasten TCPTransport tests to FakeSocket+FakeClock (<10ms)`), each gated. No push. No suppression. No skipped tests.

## Self-check (mandatory — do NOT skip)
Spawn a **verifier teammate** (fresh agent) that independently:
- Re-runs the suite from clean, reports **per-test runtime** (must be <100ms; flags any >10ms).
- Greps the TCPTransport test files for real-I/O usage (must be zero).
- Confirms `IBackoffSleeper*` is gone + `IClock` is the sole fake-time mechanism.
- Re-checks live API vehicle-spy OPEN = 0 + `make gate` green.
- Confirms no test assertions changed vs the spec.
Report the verifier's findings alongside your own.

## If you get stuck (stop + flag, don't force)
- If a test can't reach <100ms without weakening its assertion, STOP + report which test + why (don't weaken the spec).
- If extending `IClock` would break its existing consumers, STOP + report the conflict (don't silently fork).
- If `PosixSocket` can't be made behavior-identical (you find the direct-POSIX code has subtleties), STOP + report — production behavior-preservation is non-negotiable.
- Do NOT push past a red gate. Do NOT suppress. Do NOT re-introduce a parallel fake-clock.

## Notes for less-capable teams (read if you're a smaller model)
- Do the phases **in order**, committing + gating each before the next. Don't skip ahead.
- The characterization tests are your safety net: after EVERY phase, `make test` must be green. If it goes red, your change altered behavior — fix YOUR change, never the test.
- `FakeSocket` doesn't need to be fancy: a small struct with a `std::deque` of scripted `recv` chunks + a connect-outcome map is enough. Model it on how `FakeClock`/existing fakes in `test/` + `firmware/tests/` are built (grep for existing fakes to match house style).
- The production path (`PosixSocket` + `SystemClock`) must be the DEFAULT (no test-only code in production). Tests inject the fakes via the ctor.

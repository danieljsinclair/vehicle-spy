# cpp:S5421 — Global stop-flags → DI: mechanism proposal (read-only)

**Rule:** cpp:S5421 "Remove this globally visible mutable state" — 7 file-scope
`std::atomic<bool>` stop-flags mutated at runtime.
**Status:** design view for the user. NO source edits. S5421 is gated on the
user picking the mechanism.

---

## 0. The 7 sites + the one constraint that makes this hard

| # | File:line | Flag | Who SETS it | Who POLLS it |
|---|-----------|------|-------------|--------------|
| 1 | `src/pipeline/TCPTransport.cpp:28` | `g_stopRequested` | `requestStop()` (static) → from a **signal handler** | `nextLine()`/reconnect loops |
| 2 | `src/pipeline/SecureTcpTransport.cpp:24` | `g_stopRequested` | `requestStop()` (static) → tests + (future) handler | `open()`/`pollRecvOrExhaust()` |
| 3 | `src/ble/USBTransport.cpp:18` | `g_stopRequested` | `requestStop()` (static) → from a **signal handler** | recv poll loop |
| 4 | `src/cli/BLERunContext.cpp:11` | `g_running` | `signalHandler()` directly | run-loop guard |
| 5 | `src/cli/LiveRunContext.cpp:23` | `g_liveRunning` | `liveSignalHandler()` directly | live run-loop guard |
| 6 | `src/cli/TelemetryRunner.cpp:13` | `g_running` | `signalHandler()` directly | run-loop guard |
| 7 | `src/discovery/UDPDiscovery.cpp:30` | `g_discoveryStopRequested` | `requestStop()` (static) → from `discoverySignalHandler` | `poll()` loop |

**The crux:** every one of these flags is **set by a C signal handler**
(SIGINT/SIGTERM) and **polled in a hot loop**. A signal handler has no `this`,
no capture, no heap — it can only reach **static storage**. So "inject the flag
into transports" alone does not solve S5421: the handler still needs a static
anchor to write to. **Any DI mechanism must explicitly bridge
static-handler ↔ injected-instance.** That bridge is what makes option (c) below
tempting-but-unavailable and what dominates the design.

## 1. Hard constraints discovered (load-bearing)

1. **C++17, not C++20.** `CMakeLists.txt:4 set(CMAKE_CXX_STANDARD 17)`; compile
   commands confirm `-std=c++17`. → **`std::stop_source`/`std::stop_token` (C++20)
   is NOT available** without bumping the project standard. Option (c) is
   therefore off the table unless the user separately approves a C++20 bump
   (large blast radius — out of scope for a single-rule fix).
2. **Async-signal-safety is ALREADY being violated** — and S5421 is the moment to
   decide whether to fix or preserve that. Concretely, `liveSignalHandler`
   (`LiveRunContext.cpp:25`) and the sibling handlers do:
   - `std::cout << "\nReceived signal " << sigNum << ", shutting down..." << std::endl;`
   - then call `TCPTransport::requestStop()` / `USBTransport::requestStop()`.
   `std::cout`/`std::endl` are **NOT async-signal-safe** ([signal-safety(7)](https://man7.org/linux/man-pages/man7/signal-safety.7.html)).
   The atomic *store* itself is fine (atomics-in-handlers is explicitly permitted,
   [N2547](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2547.htm));
   the I/O is the latent UB. Today it "works" because Ctrl+C tends to land between
   other couts. This proposal must not make it worse, and ideally removes the I/O
   from the handler.
3. **The static `requestStop()`/`resetStop()` API is a public, test-relied seam.**
   `TCPTransport::requestStop()` / `resetStop()` (and Secure/USB equivalents) are
   called from the handlers AND from the test suites (see §4). Removing them
   changes the API the user’s tests depend on — a test-rewrite cost the user
   should see up front.

## 2. Mechanism options

### (A) `StopToken` wrapper owned by the run-context, injected into transports — RECOMMENDED

A small value type in the pipeline layer:

```cpp
// pipeline/StopToken.h
class StopToken {
public:
    void requestStop() noexcept { flag_.store(true, std::memory_order_seq_cst); }
    void reset()       noexcept { flag_.store(false, std::memory_order_seq_cst); }
    bool requested() const noexcept { return flag_.load(std::memory_order_seq_cst); }
private:
    std::atomic<bool> flag_{false};
};
```

- **Transports:** ctor takes `std::shared_ptr<StopToken>` (shared so the same
  instance is injected into every transport a run-context owns). The hot loop
  polls `token_->requested()` instead of `g_stopRequested.load()`. → sites 1/2/3.
- **Run-contexts (4/5/6):** own the `StopToken` as a member; the loop guard
  becomes `while (!stop_->requested())`.
- **Discovery (7):** `UDPDiscovery` ctor takes `std::shared_ptr<StopToken>`;
  `poll()` checks `stop_->requested()`.
- **The handler bridge (the crux):** the run-context publishes its `StopToken*`
  to a **single** file-scope pointer at `run()` entry, and the handler does ONE
  thing — `if (g_activeStop) g_activeStop->requestStop();` (a lock-free atomic
  store — async-signal-safe). The `std::cout` moves OUT of the handler.

  ```cpp
  // cli/SignalStopBroker.cpp (new, tiny)
  namespace { std::atomic<StopToken*> g_activeStop{nullptr}; }
  void brokerSet(StopToken* t) noexcept { g_activeStop.store(t); }
  extern "C" void onStopSignal(int) {            // async-signal-safe body
      if (auto* t = g_activeStop.load()) t->requestStop();
  }
  ```

  This collapses **7 globals → 1 static pointer**, and that pointer is the
  irreducible minimum a signal handler requires. S5421’s spirit is satisfied:
  transports no longer reach into TCPTransport’s/USB’s globals — they hold an
  injected token; the lone remaining static is the handler’s unavoidable anchor.

- **Async-signal-safety:** the handler body becomes a single atomic load + a
  single atomic store. That *fixes* the existing `std::cout` UB as a side
  effect. ✅
- **SRP/DRY:** one `StopToken` type replaces 7 ad-hoc atomics; one broker owns
  the handler wiring. ✅
- **Risk:** medium. Touches 7 files + adds 2 small files + ctor signature
  changes on 3 transports + `PipelineFactory.cpp:124` construction site. The
  static `requestStop()` API goes away → test rewrite (§4). Single-run-context-
  per-process invariant (already documented at `LiveRunContext.cpp:36-39`) keeps
  the single-pointer broker safe.
- **Why best:** it’s the mechanism the user asked for ("mutated globals → DI"),
  it’s C++17-native, it makes the handler async-signal-safe, and it leaves
  exactly one unavoidable static (the handler anchor) which S5421 cannot
  eliminate anyway.

### (B) Thread `std::atomic<bool>&` through ctors (no wrapper)

Same shape as (A) but no `StopToken` type — transports/run-contexts take
`std::atomic<bool>&`. Handler bridge is the same single-pointer trick but typed
`std::atomic<bool>*`.

- **Pro:** zero new types; smallest conceptual delta.
- **Con:** a raw `atomic<bool>&` is a weaker contract than a named `StopToken`
  (no `reset()`, no self-documenting name, easy to pass the wrong flag). It also
  doesn’t aggregate the discovery-vs-transport-vs-runloop flags into one token —
  you’d still have several references threaded separately, so the "7→1" win is
  weaker. Violates the Open/Closed lean the codebase already follows.
- **Risk:** low-medium. Same ctor/test churn as (A) with less encapsulation.
- **Verdict:** viable but inferior to (A). Choose only if the user wants the
  absolute minimal new abstraction.

### (c) C++20 `std::stop_source` / `std::stop_token` — NOT AVAILABLE

`std::stop_source::request_stop()` + injecting `std::stop_token` into
transports is the textbook modern answer and needs no manual handler bridge
(you can still poll `stop_token.stop_requested()` in the hot loop).

**But the codebase is C++17** (§1.1). Adopting it requires
`set(CMAKE_CXX_STANDARD 20)`, which has project-wide blast radius (re-audit of
every TU under `-Werror`, potential ABI/toolchain questions for the ESP32/iOS
builds). That is a project decision, not a single-rule refactor.

- **Verdict:** present it to the user as "the cleanest long-term mechanism, but
  it’s gated on a C++20 bump." Do not pursue under S5421 alone.

## 3. Async-signal-safety notes (must-read for the user)

- A signal handler may **only** perform operations whose entire call graph is
  async-signal-safe ([signal-safety(7)](https://man7.org/linux/man-pages/man7/signal-safety.7.html)).
  In practice that means: write to a lock-free `std::atomic` or
  `volatile sig_atomic_t`, and do **nothing else** — no `cout`, no `endl`, no
  locks, no malloc, no `std::function`.
- The "static pointer to the injected flag, dereferenced in the handler" pattern
  (used by A and B) is the standard, accepted bridge — **provided** the pointer
  load/store and the flag store are lock-free atomics. `std::atomic<StopToken*>`
  / `std::atomic<bool>` are lock-free on the target (Apple Silicon, ESP32 Xtensa
  — both provide lock-free 1-byte/pointer atomics). ✅
- Caveat to flag honestly: even "theoretically async-signal-safe" ops can page-
  fault ([Boost: theoretical async signal safety](https://www.boost.org/doc/libs/latest/doc_html/stacktrace/theoretical_async_signal_safety.html)).
  For this codebase the risk is negligible (the atomic lives in always-resident
  BSS), but it’s why some shops prefer `sigaction` + `SA_RESTART` + `EINTR`-poll
  (which `UDPDiscovery.cpp:242` already uses as a second early-exit path). The
  proposal keeps that EINTR path intact as defence-in-depth.
- **The current `std::cout` in the handlers is pre-existing latent UB**, not
  introduced by S5421. Option (A) removes it; (B) can too; (c) doesn’t address it
  directly. Worth telling the user regardless of which mechanism they pick.

## 4. Tests that lock the behaviour (the safety net)

These currently call the **static** `requestStop()`/`resetStop()` API. Under any
DI option the API moves onto the injected token, so these tests get rewritten to
hold their own `StopToken` and call `token.requestStop()` — the *behaviour* they
lock (Ctrl+C → nextLine returns nullopt promptly) is unchanged:

- `test/pipeline/TCPTransport.test.cpp` — `requestStop`/`resetStop` at
  :207/:229/:231/:238/:269/:271/:281/:305 (the "stop bounds the wait" contracts).
- `test/pipeline/SecureTcpTransport.test.cpp` — `RequestStop_TerminatesNextLine`
  + handshake/round-trip/disconnect suites (d60d769 contracts).
- `test/pipeline/USBTransport.test.cpp` — USB stop-flag contracts.
- `test/cli/TelemetryRunner.test.cpp` — runner stop behaviour.

No production behaviour change; the tests migrate from a static API to an
injected-token API. The user should know the test churn is part of the cost.

## 5. Recommendation

**Option (A) — injected `StopToken` + a single static-pointer broker for the
signal handler**, keeping the EINTR defence-in-depth path in UDPDiscovery.

Rationale:
- It is the literal "globals → DI" the user asked for; transports hold an
  injected token, not a class-static global.
- C++17-native (no standard bump), unlike (c).
- Stronger contract than the raw-ref (B): one named type, one `reset()`, one
  place to extend (Open/Closed).
- It **reduces 7 mutated globals to 1 unavoidable static anchor** — the
  irreducible minimum any signal handler needs — and makes the handler body
  async-signal-safe as a bonus (removing the existing `std::cout` UB).
- Test churn is contained and behaviour-preserving (§4).

**Open question for the user:** is removing the static
`Transport::requestStop()`/`resetStop()` API acceptable (it forces the test
re-rewrite in §4), or do they want a thin static façade retained that delegates
to the broker? The façade keeps tests as-is but leaves one class-static seam
(S5421 would partially re-flag it). My lean: remove the façade, pay the test
rewrite, get a clean zero.

---

### Sources
- [signal-safety(7) — Linux man-pages (man7.org)](https://man7.org/linux/man-pages/man7/signal-safety.7.html) — authoritative list of async-signal-safe functions.
- [N2547: Allow atomics use in signal handlers (open-std.org)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2547.htm) — the standards basis for atomic-in-handler.
- [Boost: Theoretical async signal safety](https://www.boost.org/doc/libs/latest/doc_html/stacktrace/theoretical_async_signal_safety.html) — page-fault caveat for "theoretically safe" handler ops.

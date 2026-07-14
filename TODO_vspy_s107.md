# TODO_vspy_s107.md — TCPTransport ctor cpp:S107 fix

> Read `TODO.md` first (constraints, gate, reporting). This is a small, well-scoped fix. Do it FIRST (before `TODO_vspy_fast_tests.md`) — it clears the sonar blocker.

## Goal
Clear **cpp:S107** (constructor has too many parameters) on `vehicle_sim::pipeline::TCPTransport` so vehicle-spy returns to **Sonar OPEN = 0**.

## Step 0 — verify (don't assume)
1. `make sonar-scan` (forces a fresh scan now that the stale-prereq hole is fixed).
2. Live API:
   ```
   curl -s -u "$SONAR_TOKEN_ES:" "https://sonarcloud.io/api/issues/search?componentKeys=danieljsinclair_vehicle-spy&rules=cpp:S107&statuses=OPEN&ps=500"
   ```
   Confirm: S107 is OPEN, on the TCPTransport ctor, and note the current param count + the exact param list (read `include/vehicle-sim/pipeline/TCPTransport.h`). If S107 is NOT open or is elsewhere, stop + report — don't fix the wrong thing.

## The fix — group the endpoint args
The ctor takes (among others) `host`, `port`, `adapterProtocol` — three args that ALWAYS travel together (they define the remote endpoint). Group them into a real domain object:

```cpp
// include/vehicle-sim/pipeline/TCPTransport.h
struct TransportEndpoint {
    std::string host;
    int port;
    std::string protocol;   // was adapterProtocol
};
```

The ctor becomes:
```cpp
TCPTransport(TransportEndpoint endpoint,
             std::shared_ptr<ITransportOutput> output = std::make_shared<StdOut>(),
             TcpReadTiming timing = TcpReadTiming{},
             std::shared_ptr<StopToken> stop = std::make_shared<StopToken>(),
             HuntResilienceConfig resilience = HuntResilienceConfig{});
```
→ 5 params (well under the S107 threshold of 7).

**Why this is allowed (not a band-aid):** the project's parameter-object policy forbids parameter objects as a *generic* SRP fix, but explicitly permits grouping genuinely-coupled ctor args that form a **real domain object**. `host+port+protocol` IS an endpoint — a real domain concept, always supplied together. That's the permitted case. Do NOT group unrelated args just to cut the count.

- If, when you read the ctor, you find OTHER args that are also always-coupled + form a real domain object, group those too — but `TransportEndpoint` is the user-approved + sufficient grouping. When unsure, leave args separate.

## Update all callers
- Production: grep for `TCPTransport(` constructions (CLI entry points, anywhere). Update to build a `TransportEndpoint` first.
- Tests: `test/pipeline/TCPTransport*.test.cpp` (many ctor calls). Update each. Keep assertions unchanged — this is a mechanical call-site update.
- The `#define private public` re-include trick + the `VEHICLE_SIM_HUNTING_ENABLED` public `host_`/`enterHuntingState()` exposure stay as-is.

## Also sweep (while you're in the header/tests — minor, same commit is fine)
- clangd flags unused includes: `<chrono>` in `TCPTransport.h:9` and `<functional>` in `TCPTransportHuntingCancel.test.cpp:67`. Remove if genuinely unused after your change (verify the build still compiles).

## Acceptance (all must hold)
1. cpp:S107 cleared; **vehicle-spy OPEN = 0** via the live API.
2. Full `make gate` green (test + firmware-host-tests + ios + ios-analyze + firmware + sonar-scan).
3. No behavior change — all host tests green (esp. `TCPTransportHuntingGapTest.*`, `TCPTransportHuntingTest.*`, `TCPTransportHuntingCancelTest.*`, the nextLine tests). Assertions unchanged.
4. Single commit, message: `cpp:S107 group TCPTransport endpoint args (host/port/protocol)`. End with `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`. No push.

## Self-check
Spawn a verifier teammate: re-run `make gate`, re-check live API vehicle-spy OPEN = 0, confirm no test assertions changed (`git diff` shows only ctor + call-sites + the struct). Report both your result + the verifier's.

## Do NOT
- Don't push. Don't suppress (no NOSONAR). Don't touch `TODO_vspy_fast_tests.md` work (that further touches the ctor — coordinate via single-writer-on-TCPTransport; you finish + commit first, then fast-tests starts).
- Don't reach for a bigger param-object that groups unrelated args — only the endpoint.

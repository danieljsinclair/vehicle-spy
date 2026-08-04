# SPEC — Connection & Discovery State Model v2

> Locked 2026-07-28 with user. State-first, TDD. Serial log is the diagnostic
> source of truth; LED is a simple user-facing indicator. Foundation for reliable
> autoconnect + live CAN data.

## Goals
1. **Serial** = faithful, complete mirror of all state + transitions + events.
2. **LED** = simple user status: "on WiFi?", "client connected (happy path)?", "AP mode?", "error?".
3. **Discovery cadence** = rapid when needed, ≤60s cap, resets to rapid on client disconnect.
4. Unblock reliable autoconnect + live CAN.

## 1. WiFi states — rename + merge (ONE separate commit)
Enum `WiFiState::State`, `WIFI_`-prefixed (distinguish from client states):
- `WIFI_DISCONNECTED` (was `DISCONNECTED`)
- `WIFI_CONNECTING` (was `CONNECTING`; **absorbs `RECONNECTING`** — a drop now goes to `WIFI_CONNECTING` with `tcpRestart=true`)
- `WIFI_CONNECTED` (was `CONNECTED_STA`)
- `WIFI_AP_MODE` (was `CONNECTED_AP`)
- **`RECONNECTING` removed** (merged into `WIFI_CONNECTING`; "drop vs first-connect" becomes an event, not a state).

Universal: `stateName()` (serial string matches enum, e.g. `WIFI_CONNECTED`) + every source/test/**docs** ref. Behaviour-preserving except the RECONNECTING merge. Tests referencing `RECONNECTING` updated to expect `WIFI_CONNECTING`.

## 2. Client connection state model (NEW)
`ClientState` (the user's primary interest):
- `CLIENT_IDLE` — no client.
- `CLIENT_AUTHENTICATING` — TCP accepted, checking auth.
- `CLIENT_CONNECTED` — authed, active.
Transitions (capture client IP at accept; carry through events):
- `CLIENT_IDLE → CLIENT_AUTHENTICATING` (accept + IP)
- `CLIENT_AUTHENTICATING → CLIENT_CONNECTED` (auth ok)
- `CLIENT_AUTHENTICATING → CLIENT_IDLE` (auth fail → `auth_fail` event, IP + reason)
- `CLIENT_CONNECTED → CLIENT_IDLE` (drop → `client_disconnected` event, IP + reason; **reset discovery backoff**)

## 3. Serial contract (the observability gate)
Periodic (every 5s):
```
[STATE] uptime=5000ms wifi=WIFI_CONNECTED client=192.168.1.42 disc=10s led=SOLID_BLUE monitor=idle
```
(`client=none` when no client; `disc=`=current cadence; `led=`=current pattern; `monitor=`=CAN monitor)
One-shot events:
```
[EVENT] wifi_connected      ip=192.168.1.42
[EVENT] wifi_drop           reason=4
[EVENT] client_connected    ip=192.168.1.50
[EVENT] auth_fail           ip=192.168.1.50 reason=bad_token
[EVENT] client_disconnected ip=192.168.1.50 reason=reset_by_peer
[EVENT] discovery_broadcast cadence=10s n=42
```

## 4. LED semantics (user-facing; single blue LED)
Primary: "on WiFi?" + "client connected?". Detail → serial.
- **Solid blue = `CLIENT_CONNECTED`** (happy path, product ready). ← primary
- [pattern] = `WIFI_CONNECTED` + no client (waiting for client)
- [pattern] = `WIFI_AP_MODE`
- [pattern] = `WIFI_CONNECTING` / `WIFI_DISCONNECTED` (searching / no WiFi)
- [error sequence] = auth fail / error
Patterns live in the declarative table (user-tunable). `selectLedPattern(wifiState, clientState)` prioritises client-connected = solid. Supersedes the per-wifi-state mapping.

## 5. Discovery backoff (rework)
| Age since connect | Interval |
|---|---|
| 0–2 min | **500 ms** (rapid) |
| 2–5 min | 10 s |
| 5–10 min | 30 s |
| >10 min | **60 s (hard cap)** |
- Time-based; **ignores client-connected** (a client doesn't slow it).
- **Last client disconnects → reset to rapid.**
- 30-min tier deleted.

## 6. Trust
- Connect immediately — no trust gate on a bare IP (no crypto yet).
- iOS UI: **yellow "untrusted"** badge now → **green "Trusted"** when Ed25519 discovery signing is added (deferred).

## 7. iOS work (separate)
- Audit connection unreliability ("connects sometimes") as a **program-state/retry** issue, not only the NWListener receiver (#132). Inspect the discovery-listen / autoconnect / reconnect state.
- Autoconnect: try last-known device IP first, fall back to discovery.
- Trust badge UI per §6.

## Execution order (spec-first, TDD; one commit per area)
1. **Rename** (§1) — universal, behaviour-preserving (+ RECONNECTING merge). **Separate commit.**
2. **Client SM + serial `[STATE]`/`[EVENT]`** (§2, §3) — blind tests first.
3. **LED semantics** (§4) — blind tests (Pattern enum).
4. **Backoff rework** (§5) — blind tests.
5. **iOS** (§7) — separate stream.

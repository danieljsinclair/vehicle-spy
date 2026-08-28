# WiFiManager / WiFiState Refactor Roadmap

Planned future refactors for `firmware/vanilla/WiFiManager.cpp/.h` and the WiFi
state model. These are too invasive to bolt onto a bug-fix change — they need
their own dedicated pass with full test coverage. Revisit the next time this
code is touched in any significant way.

The current code (post auth-fallback fix + SRP refactor) is clean, tested
(411 firmware tests green), and behavior-stable. These refactors are about
long-term maintainability, not correctness.

---

## 1. Recovery-policy column on `ReasonEntry` (OCP for reason classification)

**File:** `firmware/vanilla/WiFiReasonCodes.h`

**Problem:** Two predicates classify disconnect reasons by the *action* the
state machine should take: `isAuthMechanismFailure()` (15/23/202 -> run the
strategy campaign) and `isLinkLevelDrop()` (0/2/3/4/24/200/201/204 -> retry STA
forever). Both are hardcoded literal sets. Adding a new reason means updating
both sets by hand — OCP violation, and the two sets can drift.

**Existing structure:** `kReasonTable[]` already maps each reason code to a
`phase` string (`"auth"`, `"link"`, `"assoc"`, `"handshake"`, `"unknown"`).
But the phase axis (which 802.11 layer failed) does NOT line up with the
action axis (what to do about it): e.g. phase `"handshake"` contains both 14
(MIC_FAILURE -> auth campaign) and 204 (HANDSHAKE_TIMEOUT -> link-level). So
phase alone can't drive the classification.

**Planned fix:** Add a third column to `ReasonEntry` — an enum:

```cpp
enum class Recovery { AuthCampaign, RetryForever, Neutral };
// AuthCampaign: run the strategy rotation, escalate to AP on exhaustion
// RetryForever:  link-level drop, keep retrying STA, never escalate
// Neutral:        neither (e.g. ASSOC_TOOMANY, MESH_STEER_REJECT) — default handler
```

Then derive both predicates from it:
- `isAuthMechanismFailure(r) = recovery(r) == Recovery::AuthCampaign`
- `isLinkLevelDrop(r) = recovery(r) == Recovery::RetryForever`

Adding a new reason becomes ONE row in `kReasonTable` with its recovery
policy. Both predicates stay correct by construction.

**Prerequisite:** Full test coverage of both predicates at the three corners
(true-codes, false-for-auth, false-for-unrelated) — already present
(`IsLinkLevelDrop_*`). Add a test that asserts the two sets are disjoint and
cover every entry in `kReasonTable` (no reason left `Neutral` by accident).

**Risk:** Medium. Touches the shared reason table used by the serial tracer
(`wifiReasonName`, `wifiReasonPhase`). Must keep the existing `phase` column
working unchanged. Do NOT attempt without the disjointness/coverage tests in
place first — exactly the kind of invasive refactor that's easy to screw up.

---

## 2. WiFiState full State Pattern (eliminate switch-on-state)

**Files:** `firmware/vanilla/WiFiManager.h`, `StatusLED.h`, `StatusLED.cpp`

**Problem:** The State Pattern is half-applied. There are state-specific
*handlers* (`ConnectingStateHandler` etc.) but the code still has
`switch(on state)` in at least 4 places:
- `WiFiManager::getStateHandler()` — maps state -> handler
- `WiFiManager::stateName()` — maps state -> display name
- `WiFiState::isApModeState()` — special-cases the two AP ordinals
- `StatusLED::selectLedPattern()` — maps state -> LED pattern

Every `switch` is a maintenance hazard: adding a state means finding and
updating all of them.

**Planned fix:** Make `WiFiState` an abstract base with virtuals:
```cpp
class WiFiState {
public:
    virtual ~WiFiState() = default;
    virtual std::string name() const = 0;
    virtual bool isApMode() const = 0;
    virtual int ledPattern() const = 0;
    virtual StateTransition execute(uint32_t now, Context& ctx) = 0;
};
```
Each state becomes a concrete subclass (`DisconnectedState`,
`ConnectingState`, `ConnectedStaState`, `ApModeDefaultState`,
`ApModeAuthFailState`). Then:
- `getStateHandler()` disappears — the state *is* the handler.
- `stateName()` is `state->name()`.
- `isApModeState()` is `state->isApMode()`.
- `selectLedPattern()` is `state->ledPattern()`.

**Risk:** HIGH. This is the biggest rewrite. Changes the Context ownership
model, the `WiFiManager` constructor, and every test that constructs or
drives `WiFiManager`. Must be a dedicated sprint with the full 411-test suite
as the safety net. Do NOT half-do it — a half-converted State Pattern is worse
than the current consistent (if repetitive) switches.

**Suggested order:** do refactor #1 first (it's bounded and testable), then
tackle this one with the reason classification already table-driven.

---

## 3. State-metadata table (derive name/pattern/isApMode from one source)

**Files:** `firmware/vanilla/WiFiManager.h`, `StatusLED.h`

**Problem:** Even without the full State Pattern (#2), the per-state metadata
(name string, LED pattern, isApMode flag) is scattered across `stateName()`,
`selectLedPattern()`, and `isApModeState()`.

**Planned fix (lighter-weight alternative to #2):** A single
`kStateTable[]` array indexed by state ordinal:

```cpp
struct StateMeta { State state; const char* name; int ledPattern; bool isApMode; };
static constexpr StateMeta kStateTable[] = { ... };
```

`stateName()`, `selectLedPattern()`, `isApModeState()` all become table
lookups. Adding a state is one row. This is a smaller step that doesn't
require the full polymorphism of #2, and it's compatible with doing #2 later
(the virtuals just read from the same table).

**Risk:** LOW. Contained, mechanical, easy to test. Can be done independently
of #2. Recommended as a quick win if #2 is too big for the current sprint.

---

## Suggested priority order

1. (Now) Auth-fallback fix + SRP refactor — DONE.
2. State-metadata table (#3) — low risk, quick win, enables #2.
3. Recovery-policy column (#1) — bounded, needs tests first.
4. Full State Pattern (#2) — big rewrite, last, after #1 and #3 de-risk the
   state model.

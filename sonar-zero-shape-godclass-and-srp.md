# Decomposition Shapes — BLEManagerBase God-Class + VehicleSignal 11-param ctor

**Author:** Second-Opinion Agent
**Date:** 2026-06-30
**Branch:** `sonar_fixes`
**Mandate (user directives):** "no god classes" AND "avoid param objects — SRP smell."
**Read-only.** Every claim grounded in source read this session + cited community evidence. No source edited.

---

## Item 1 — S3656 + S1448: `BLEManagerBase` god-class

**The smell.** `BLEManagerBase` (include/vehicle-sim/ble/BLEManagerBase.h) carries **5 distinct responsibilities** behind one class:
1. **BLE transport** (scan/connect/disconnect/send/isConnected) — the genuinely platform-virtual core.
2. **Device discovery cache** (`discovered_devices_`, `addDiscoveredDevice`/`clearDiscoveredDevices`/`findDeviceByAddress`, `device_callback_`).
3. **Connection state** (`connected_`, `connected_device_id_`, `setConnectionState`, `connection_callback_`).
4. **OBD2 / ELM327 protocol layer** (`obd2_protocol_`, `queryPID`, `initializeELM327`, `processOBD2Data`, `parseASCIIResponseToBinary`, `sendASCII`, `sendPromptDrivenSequence`, `waitForPrompt`/`notifyPrompt`).
5. **OBD2 polling + CAN monitor + VIN + auto-detection** (`polling_thread_`, `startOBD2Polling`/`stopOBD2Polling`, `can_mode_`, `startCANMonitor`, `queryVIN`, `initializeOBD2WithDetection`, `vehicle_detector_`).

That is the textbook god-class: ~40 methods, 17 protected/private fields, 5 reasons to change (violates SRP) — exactly what `S3656` (god-class) and `S1448` (too many methods/fields) flag.

### Evidence: the subclass coupling is narrow — and that's the lever
The two subclasses (`BLEManagerMacOS.mm`, `BLEManageriOS.mm`) touch **only roles 2 and 3**, never 4 or 5:
- macOS: zero references to `obd2_protocol_`, `polling_*`, `prompt_*`, `can_mode_`, `vehicle_detector_`, `queryPID`, `waitForPrompt`, etc.
- iOS: exactly **one** call into role 4 (`return BLEManagerBase::initializeELM327();` at `BLEManageriOS.mm:380`).

Both subclasses' protected-member access clusters in exactly **two** groups: the device cache (`discovered_devices_`, `addDiscoveredDevice`/`clearDiscoveredDevices`/`findDeviceByAddress`) and connection state (`connected_`/`connected_device_id_`, `setConnectionState`, `invoke*Callback`). Verified by grep across both `.mm` files this session.

**This is the key finding:** roles 4 and 5 (OBD2 protocol + polling/CAN/VIN/detection) have **zero subclass surface** — they are pure base-class behaviour. That makes them cheaply extractable into composed components with no subclass breakage. Roles 2 and 3 are shared with subclasses, so they must be exposed via **role interfaces** (the user's explicit directive: preserve subclass access through interfaces, NOT a param-bag).

### Alternative shapes

#### A — Role-interface extraction (ISP), inheritance preserved (RECOMMENDED)
Keep `BLEManagerBase` as the slim polymorphic transport core (the only thing that genuinely needs platform overrides: `scanForDevices`/`connect`/`disconnect`/`send`/`isConnected`). Extract the other four responsibilities into **composed components**, each behind a small role interface. Subclasses get access through those interfaces (composition-by-reference), not by reaching into protected fields:

```cpp
// New role interfaces (ISP) — each one reason to change
struct IBleDiscoveryCache {                       // role 2
    virtual void add(const BLEDeviceInfo&) = 0;
    virtual void clear() = 0;
    virtual std::optional<BLEDeviceInfo> findByAddress(std::string_view) const = 0;
    virtual std::vector<BLEDeviceInfo> snapshot() const = 0;
    virtual ~IBleDiscoveryCache() = default;
};
struct IBleConnectionState {                      // role 3
    virtual void set(bool connected, std::string_view deviceId) = 0;
    virtual bool isConnected() const = 0;
    virtual std::string deviceId() const = 0;
    virtual ~IBleConnectionState() = default;
};
class Elm327Session { /* roles 4+5 fused: owns obd2_protocol_, prompt seq, polling, CAN, VIN, detector */ };
```
`BLEManagerBase` becomes the transport core + **holds references** to a `IBleDiscoveryCache`, `IBleConnectionState`, and an `Elm327Session`:
```cpp
class BLEManagerBase {                       // now ~10 methods, not 40
protected:
    BLEManagerBase(IBleDiscoveryCache&, IBleConnectionState&);  // subclasses pass concrete impls
    Elm327Session& elm327();                 // roles 4+5 reachable, but owned, not smeared into the base
    // discovery + connection-state reached via the injected references (typed interfaces, not a param-bag)
};
```
Subclasses stop touching `discovered_devices_`/`connected_` directly; they call `discovery().add(...)`, `connection().set(...)`. The callbacks (`device_callback_`/`data_callback_`/`connection_callback_`) move onto whichever role owns their emission (discovery/connection) — they're a transport-agnostic concern, not a fourth field-blob.

- **Clears S3656** (no god-class) and **S1448** (each class ≤ a handful of methods).
- **Honours "no param objects":** access is via distinct typed role interfaces, one per concern — not a single `BleContext`/param-bag. (The C2 wiki rule of thumb: *"if you want to expose the interface of a lower-level class, use inheritance; if you merely want to use it, use composition"* — roles 4/5 are *used*, so composition; roles 2/3 are *exposed to subclasses*, so interface + composition.)

#### B — Pure composition, drop inheritance
Make `BLEManagerMacOS`/`BLEManageriOS` **not** subclass anything; each composes an `IBleTransport` strategy + the four role components. Maximally decoupled (true composition-over-inheritance), but it is the largest churn: every virtual override (`scanForDevices`, `connect`, …) becomes a strategy injection, and the two `.mm` files need a full rewrite. Higher risk for no extra S3656 credit over A — **not recommended** unless a third platform is imminent.

#### C — Incremental "extract role 4+5 only" (lowest-risk landing path)
The single highest-value, lowest-risk move: extract **roles 4 and 5** (OBD2 protocol + polling/CAN/VIN/detection — the ~20 methods and `obd2_protocol_`/`polling_*`/`can_mode_`/`vehicle_detector_`/`prompt_*` state) into a composed `Elm327Session` component. Because **zero subclass code touches these**, this is a mechanical move with no subclass signature changes — it halves the god-class (40→~20 methods) and removes the most state-heavy members, all behind the existing public OBD2 API. Defer the discovery/connection-state role-interface split (A) to a second PR.

### Recommendation
**A is the target; C is the landing path.** Land C first (extract `Elm327Session` — zero subclass risk, immediate S3656/S1448 relief), then complete A (discovery-cache + connection-state role interfaces) in a follow-up gated by the subclass rewrite. Do not pursue B unless a third platform lands.

### Risk + tests
- **Risk (C):** Low — pure base-class behaviour moves to a sibling component; subclass code untouched. The public OBD2 API (`queryPID`/`initializeELM327`/`startOBD2Polling`/`queryVIN`/`processOBD2Data`) is preserved as thin forwards on `BLEManagerBase` so callers don't change.
- **Risk (A):** Medium — both `.mm` subclasses rewrite their `discovered_devices_`/`connected_`/`invoke*Callback` access to go through the role interfaces. Mechanical but touches Apple-specific code (CoreBluetooth callbacks).
- **Existing tests:** `test/ble/BLEManager.test.cpp` has **21 tests** — they lock the public OBD2/discovery/connection behaviour (not the internal state layout), so they should pass through both C and A unchanged. Add: a `Elm327Session` unit test (polling loop, prompt sequencing, CAN-mode toggle) once C lands — currently untestable in isolation because it's smeared across the base.

---

## Item 2 — S107: `VehicleSignal` 11-parameter constructor

**The smell.** `VehicleSignal` (src/domain/VehicleSignal.cpp:5) takes **11 positional ctor params** — `timestampUtcMs` + 10 `std::optional` telemetry fields. S107 fires because the ctor has >7 params. The user's read is correct in spirit: the fields are *thematically* three groups:
- **Driving/dynamics:** throttlePercent, speedKmh, accelerationG, brakePercent, steeringAngleDeg.
- **Powertrain (motor):** motorRpm, motorHvVoltage, motorHvCurrent, motorTorqueNm.
- **Driveline state:** gearSelector.

### Evidence: but `VehicleSignal` is a serialization DTO, not a domain aggregate
Before recommending a split, I traced **every reader** (grep across `src/`). The decisive findings:
- **Two boundary sinks read ALL 10 fields every time:** `telemetry/TraceLogger.cpp` (the fixed **11-column CSV** schema — timestamp + 10 telemetry columns) and `presentation/VehicleSignalFormatter.cpp` (the canonical formatted record). Both iterate every getter.
- There are **5 direct construction sites** (`VehicleSignal.cpp`, `OBD2SignalTranslatorBase.cpp:47`, `VehicleSignalFactory.cpp:143`, `DemoSignalProvider.cpp:139`, `VehicleSim.cpp:70`).
- The header doc-comment is explicit and load-bearing: *"This is the only signal format that crosses the boundary layer… standard OBD2 normalized format."* It is a **wire/serialization value object** whose entire purpose is to carry the complete column set to the CSV/UI sinks.
- A parallel `TelemetrySignal` (rpm/gear/torque — the "translated powertrain" view) **already exists** as a separate, smaller value object. So the domain already separates the powertrain-translated view from the raw OBD2 boundary DTO.

**Consequence:** splitting `VehicleSignal` into `DrivingTelemetry` + `PowertrainTelemetry` + `GearState` (the naive SRP decomposition) would be net-negative here: every one of the boundary sinks (`TraceLogger`, `VehicleSignalFormatter`) would have to **re-aggregate the three halves** back into the same flat 11-column row — moving complexity from one ctor to N readers, and breaking the "one boundary type" contract the header documents. That is the trap the user's "avoid param objects" directive is really warning against: don't invent a `VehicleSignalCtorParams` bag either.

### So the right SRP move is NOT "split the DTO" — it's "stop forcing callers to name 11 positionals." Three alternatives:

#### A — Named-field builder / fluent construction (RECOMMENDED)
Keep the **one** boundary type (honouring the header contract and the all-field readers), but replace the 11-positional ctor with a builder that takes cohesive sub-groups as **value-struct arguments** — structs that are genuine domain groupings (SRP), not a param-bag:

```cpp
struct DrivingDynamics {   // cohesive: driver-input + chassis motion
    std::optional<double> throttlePercent, speedKmh, accelerationG, brakePercent, steeringAngleDeg;
};
struct PowertrainState {   // cohesive: motor + HV bus
    std::optional<double> motorRpm, motorHvVoltage, motorHvCurrent, motorTorqueNm;
};
class VehicleSignal final {
public:
    struct Snapshot { std::uint64_t timestampUtcMs; DrivingDynamics driving; PowertrainState powertrain; std::optional<std::int32_t> gearSelector; };
    explicit VehicleSignal(Snapshot s) noexcept;   // 1 param — clears S107
    // ...OR a fluent builder: VehicleSignal::at(ts).driving({...}).powertrain({...}).gear(...)
};
```
- **Why this is SRP, not a param-bag:** the three sub-structs each represent *one cohesive telemetry domain* with its own small (≤5-param) ctor. `DrivingDynamics` and `PowertrainState` are independently meaningful value types (and `PowertrainState` overlaps the existing `TelemetrySignal` concept — a possible future unification). This is the GoF/ISP reading: the aggregate composes cohesive parts; it is not a flat `Params` bag of unrelated fields.
- **Clears S107** (the public ctor now takes 1 arg, or the builder caps any single call at ≤5).
- **Construction sites** become more readable and less error-prone (positional `std::nullopt`×6 in `OBD2SignalTranslatorBase.cpp:47` becomes a partial `DrivingDynamics`/empty `PowertrainState` — intent is explicit).
- **Readers unchanged:** `TraceLogger`/`VehicleSignalFormatter` still call the same 10 getters on the same single type; no re-aggregation needed.

#### B — Aggregate-by-value sub-struct members (deeper SRP)
Make `VehicleSignal` *contain* the sub-structs as members and delegate the getters (`getMotorRpm()` → `powertrain_.motorRpm`). Same external API, but the internals are decomposed. More churn (every getter body changes) for no extra S107/SRP benefit over A, and it touches the `operator==` member-wise comparison. **Only worth it if you also want to expose `driving()`/`powertrain()` sub-views** to new consumers.

#### C — Keep the 11-param ctor, silence S107 via a default-arg aggregate (NOT recommended)
`explicit VehicleSignal(std::uint64_t, TelemetryFields fields = {})` where `TelemetryFields` is a plain aggregate of the 10 optionals. This is **exactly the param-bag the user forbade** — it hides the SRP smell behind one argument. Rejected.

### Recommendation
**A (named-field builder / `Snapshot` of cohesive sub-structs).** It resolves S107, it is genuine SRP (cohesive domain sub-types, not a bag), it preserves the single boundary-crossing type that `TraceLogger`/`VehicleSignalFormatter` depend on, and it makes the 5 construction sites safer (no more 6× positional `std::nullopt`). Do not split the DTO into separate types (would force reader re-aggregation); do not introduce a flat param-bag (forbidden).

### Risk + tests
- **Risk:** Low–Medium. The type stays a single value object; getters and `operator==` are unchanged, so the 11-column CSV output is byte-identical. The 5 construction sites each migrate to the `Snapshot`/builder form (mechanical).
- **Existing tests:** `test/domain/VehicleSignal.test.cpp`, `VehicleSignalEV.test.cpp`, `VehicleSignalFactory.test.cpp` (existing suite), `VehicleSignalFormatter.test.cpp` lock construction + every getter + equality + the 11-column format. These directly guard the migration; add one test asserting the builder/Snapshot produces the same `operator==` result as the legacy positional ctor during the transition.

---

## Cross-cutting notes
- **Both items are design refactors, not analyzer-satisfying one-liners** — the user will view then TDD-gate + execute. The shapes above are sized to land incrementally (BLE: C then A; VehicleSignal: A only).
- **BLE god-class has a uniquely clean seam** (subclass coupling confined to 2 of 5 roles) — exploit it; don't do a full inheritance→composition rewrite (B) for no extra credit.
- **VehicleSignal is a deliberate boundary DTO** — the SRP fix is at the *construction* boundary (cohesive sub-struct args), not at the *type* boundary (don't split the type). The existing `TelemetrySignal` proves the codebase already separates domain views.
- **No build collision with coder2's S5421 P2** (transport StopToken work): these shapes touch `ble/BLEManagerBase.*`, `ble/platform/*.mm`, and `domain/VehicleSignal.*` — disjoint from the transport sources coder2 is editing.

---

## Sources
- [SO — Interface inheritance to break up god objects (ISP)](https://stackoverflow.com/questions/14775767/interface-inheritance-to-breakup-god-objects)
- [SO — Process/steps to refactor a god class in a C++ legacy application](https://stackoverflow.com/questions/9647147/process-or-steps-to-refactor-god-class-in-a-c-legacy-application)
- [C2 Wiki — Composition Instead Of Inheritance](https://wiki.c2.com/?CompositionInsteadOfInheritance)
- [C2 Wiki — Use Composition And Interfaces Without Class Inheritance](https://wiki.c2.com/?UseCompositionAndInterfacesWithoutClassInheritance)
- [Strategy Pattern — Composition over Inheritance (One Wheel Studio)](https://onewheelstudio.com/blog/2020/8/16/strategy-pattern-composition-over-inheritance)
- [Ruby Pigeon — Refactoring From Inheritance To Composition](https://www.rubypigeon.com/posts/refactoring-inheritance-composition-data/)
- [Software Engineering SE — Origins of "favor composition over inheritance"](https://softwareengineering.stackexchange.com/questions/65179/where-does-this-concept-of-favor-composition-over-inheritance-come-from)

---

**Read-Only Analysis provided by Second-Opinion Agent. No source edited.**

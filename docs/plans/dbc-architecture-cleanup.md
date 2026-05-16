# DBC-Driven Architecture Cleanup — Implementation Plan

**Status**: Ready for another team to execute in a separate worktree
**Branch base**: `mvp`
**Do NOT push until user approves**

---

## Team Makeup and Roles

Follow the established project team pattern. Four agents, each with a distinct responsibility:

| Role | Agent Type | Responsibility |
|------|-----------|----------------|
| **team-lead** | claude (default) | Delegation and orchestration ONLY. No hands-on code. Assigns tasks, reviews output, integrates. |
| **critic** | general-purpose | Reviews ALL code for SOLID (SRP, DI, OCP), DRY, separation of concerns. Reviews after each implementation task. Has final say before any code is considered done. |
| **test-architect** | general-purpose | Reviews tests separately from implementation. Ensures tests add real business value, not coverage vanity. Tests written blind to acceptance criteria, not informed by implementation. |
| **implementer** | general-purpose | Writes production code. Handed specific file paths and specs. |

### Rules (from established project conventions)

- Team lead does NOT edit files, run builds, or write code
- Critic reviews ALL code — SOLID, DRY, separation of concerns
- Test architect and implementer are ALWAYS different agents
- Tests written blind to acceptance criteria, not informed by implementation
- RED phase tests MUST compile; only GREEN tests committed
- Tests assert correct behavior, not fragile error messages
- Error assertions focus on intent, not exact message strings
- Tests must test real production code, not mock code or truisms
- Tests should not depend on live data or transient files
- Tests should not depend on other tests succeeding
- Tests should honor SRP — one test, one behavior
- Do NOT kill unresponsive agents — alert user, continue with others

### SOLID Enforcement (critic's checklist)

Every code change must pass these gates:

- **SRP**: Each class has exactly one reason to change. `VehicleSimWrapper` must not be BLE manager + demo orchestrator + signal cache + mode router + DBC provider. Each class does ONE thing.
- **OCP**: New data sources (demo, BLE, file replay) added by creating new classes, NOT by adding `if` branches to existing code. `isDemoMode` boolean is an OCP violation. Use Types, Factories, and the Strategy pattern — avoid conditionals.
- **DI**: Dependencies injected via constructor, not constructed internally. `VehicleSimWrapper` must not `make_unique<VehicleSimulator>()` etc. Enable testability through constructor injection.
- **DRY**: DBC data exists in exactly one place (the submodule). No embedded string copies. No duplicate `gearCodeMappings` alongside DBC VAL_ table. Single source of truth for every mapping.
- **Separation of concerns**: iOS bridge is a thin UI veneer — presentation and API control only. No DBC content, no signal storage, no domain logic. The bridge knows nothing about how data is produced.
- **KISS**: Prefer simple, direct solutions. No python scripts to generate C++ code. No build-time code generation. If a file is a resource, bundle it as a resource.

### TDD / Red-Green-Refactor Rules

- Even RED phase tests MUST compile. We are asserting correct behavior, not asserting failure.
- Tests assert correct behavior, not fragile error messages.
- Error assertions focus on intent (exception type, key data), not exact message strings.
- Tests must test real production code, not mock code, external code, or truisms.
- Tests should not construct scenarios that always pass (testing themselves).
- Tests should use mocks to control specific business scenarios, not to verify mock behavior.
- Tests should not rely on live data or transient files not part of source code.
- Tests should not depend on other tests succeeding.
- Tests honor SRP — one test, one behavior.
- Happy path prioritized over edge cases, but reasonable exceptions tested.
- Coverage is important, but value is more important.

### Test Architect Acceptance Criteria

Tests must be:
- **Objective**: Pass/fail is unambiguous. No "looks right" tests.
- **Automatable**: Every acceptance criterion in §12 must have a corresponding automated test. The "acid test" (live Tesla drive) is the ONLY manual acceptance criterion.
- **Business-value focused**: Test that gear 4 → AUTO_1, not that a mock returns what was injected. Test the DBC pipeline end-to-end with synthetic CAN frames.
- **SRP-compliant**: One test, one behavior. No test verifies three things.
- **Independent**: No test depends on another test's success.

### Critic's Final Gate

Before any code is considered done, the critic must verify:
1. No `isDemoMode` boolean anywhere in the codebase (C++ or Obj-C/Swift)
2. No embedded DBC string literals in iOS code
3. No `gearCodeMappings` in VehicleConfig — DBC VAL_ table is single source
4. `Gear.h` constants are used everywhere (no raw magic numbers)
5. iOS bridge has zero domain logic (no VehicleSimulator, BLEManager, DBCTranslationService construction)
6. All dead-code files actually deleted (grep confirms zero references)
7. `make update-dbc` target works and pulls from submodule
8. Binary size reduced (confirm dead code is gone)

---

## 0. Situation Brief

### Two competing CAN decoder architectures

```
PRODUCTION PATH (--connect):
  BLE → onDataReceived → DBCTranslationService.processFrame()
    → DBCSignalTranslator.translate() → VehicleSignalFactory.build()
    → EventDispatcher.dispatch() → TraceLogger + stdout

LEGACY PATH (DEAD — never reached from --connect or iOS):
  CANSignalDecoderBase / CANTranslatorBase
    → AudiMLBTranslator / TeslaCANTranslator
  TeslaSignalTranslator (mock byte format, timestamp=0 placeholder)
  TeslaSignalParser (mock byte format)
  SignalTranslatorFactory (routes to the above 6 dead classes)
```

The legacy files compile and link but are unreachable. They increase binary size and maintenance surface.

### The acid-test gap

`resources/dbc/Model3CAN.dbc` is currently an 886-byte hand-crafted 3-signal stub (already replaced on disk with the full opendbc file — see §1.5). The full `tesla_can.dbc` from commaai/opendbc has **28 CAN messages** including `DI_gear` (gear selector) and `DI_analogSpeed` (road speed) — both missing from the old stub.

### DBC data must come from GitHub

The user explicitly stated: **"Don't hand-craft CAN data. Pull it from GitHub repos."**
The canonical source is `commaai/opendbc` on GitHub. DBC files must be traceable to a git submodule commit, not manually edited or copied from a local research cache.

### Current uncommitted state

`resources/dbc/Model3CAN.dbc` is already modified on disk (54,247 bytes, full opendbc content). This was copied from `.claude/research/opendbc/` during research. The implementing team should **discard this** and instead set up the git submodule properly, then copy from `external/opendbc/`. This ensures the file is traceable to a specific GitHub commit.

---

## 1. Research Findings

### 1.1 Tesla Gear Selector — DI_gear and DI_gearRequest

**Source**: commaai/opendbc `master` — `opendbc/dbc/tesla_can.dbc`
**URL**: https://github.com/commaai/opendbc/blob/master/opendbc/dbc/tesla_can.dbc

```
BO_ 280 DI_torque2: 6 DI
 SG_ DI_gear        : 12|3@1+ (1,0) [0|0] "" NEO   <-- actual/current gear
 SG_ DI_gearRequest : 28|3@1+ (1,0) [0|0] "" NEO   <-- requested gear

VAL_ 280 DI_gear         7 "DI_GEAR_SNA" 4 "DI_GEAR_D" 3 "DI_GEAR_N" 2 "DI_GEAR_R" 1 "DI_GEAR_P" 0 "DI_GEAR_INVALID"
VAL_ 280 DI_gearRequest  7 "DI_GEAR_SNA" 4 "DI_GEAR_D" 3 "DI_GEAR_N" 2 "DI_GEAR_R" 1 "DI_GEAR_P" 0 "DI_GEAR_INVALID"
```

**Raw CAN values** (same for both signals): 0=INVALID, 1=P, 2=R, 3=N, 4=D, 7=SNA.

**Confirmed from opendbc** (`opendbc/car/tesla/values.py` GEAR_MAP):
```python
GEAR_MAP = {
    "DI_GEAR_INVALID": CarState.GearShifter.unknown,
    "DI_GEAR_P": CarState.GearShifter.park,
    "DI_GEAR_R": CarState.GearShifter.reverse,
    "DI_GEAR_N": CarState.GearShifter.neutral,
    "DI_GEAR_D": CarState.GearShifter.drive,
    "DI_GEAR_SNA": CarState.GearShifter.unknown,
}
```

**Critical**: Tesla Model 3/Y has a **single-speed reduction gearbox**. There are NO forward gear numbers (1, 2, 3). The gear selector only has P/R/N/D. Other vehicles (manuals, multi-speed autos) WILL have gears 1, 2, 3+.

### 1.2 Gear representation at the API layer

The API emits **numeric constants with idiomatic names**. The display layer maps to labels.

**Gear.h canonical constants:**
```cpp
namespace Gear {
    static constexpr int32_t PARK    = -2;
    static constexpr int32_t REVERSE = -1;
    static constexpr int32_t NEUTRAL =  0;
    static constexpr int32_t GEAR_1  =  1;   // also DRIVE for single-speed auto
    static constexpr int32_t GEAR_2  =  2;
    static constexpr int32_t GEAR_3  =  3;
    // ... GEAR_4 through GEAR_N as needed

    // AUTO mode qualifier (high bit)
    static constexpr int32_t AUTO_BIT = 0x1000;
    static constexpr int32_t AUTO_1 = AUTO_BIT | 1;  // 0x1001
    static constexpr int32_t AUTO_2 = AUTO_BIT | 2;  // 0x1002
    static constexpr int32_t AUTO_3 = AUTO_BIT | 3;  // 0x1003
    // ...
}
```

**Tesla-specific mapping** (CAN DI_gear raw → API constant):
| CAN Raw | DBC Label | API Constant | API Value | Display |
|---------|-----------|-------------|-----------|---------|
| 0 | DI_GEAR_INVALID | *(nullopt)* | — | "—" |
| 1 | DI_GEAR_P | `Gear::PARK` | -2 | "P" |
| 2 | DI_GEAR_R | `Gear::REVERSE` | -1 | "R" |
| 3 | DI_GEAR_N | `Gear::NEUTRAL` | 0 | "N" |
| 4 | DI_GEAR_D | `Gear::AUTO_1` | 0x1001 (4097) | "D" |
| 7 | DI_GEAR_SNA | *(nullopt)* | — | "—" |

**Why AUTO_1 for Tesla Drive?** Tesla has a single-speed reduction gearbox. "D" means "Drive in automatic mode, gear 1". Using `AUTO_1` (0x1001) rather than plain `GEAR_1` (1) distinguishes automatic from manual gear selection. This matters for vehicles that support both modes.

**For other vehicles**: If a vehicle's DBC defines gear 1=First, 2=Second, etc., the factory emits `Gear::GEAR_1` (1), `Gear::GEAR_2` (2), etc. If the vehicle has auto mode, the factory emits `Gear::AUTO_1` (0x1001), etc.

**DI_gear vs DI_gearRequest**: Both signals use the same VAL_TABLE. The API exposes both — `gearSelector` (actual/current) and `gearRequested` (what driver/autopilot wants). UI can show "D" when driver selects Drive even before transmission engages.

**Why AUTO_1 for Drive?** Tesla has a single forward gear. When the DBC says "D", it means "Drive in automatic mode, gear 1". Using `AUTO_1` (0x1001) rather than plain `GEAR_1` (1) distinguishes automatic from manual gear selection. This matters for vehicles that support both modes.

**For other vehicles with manual gears**: If a vehicle's DBC defines gear 1=First, 2=Second, etc., the factory emits `Gear::GEAR_1` (1), `Gear::GEAR_2` (2), etc. If the vehicle also has auto mode, the factory emits `Gear::AUTO_1` (0x1001), etc.

**Translation boundary**: The DBC VAL_ table is the single source of truth. The `DBCSignalMapper` reads the parsed value table and translates raw CAN values to API gear constants. No separate `gearCodeMappings` config. The API always emits the same canonical numbers regardless of vehicle.

**DI_gear vs DI_gearRequest**: Both signals use the same VAL_TABLE. The API should expose both — `gearSelector` (actual) and `gearRequested` (requested). This allows the UI to show "D" when the driver selects Drive, even before the transmission engages.

### 1.3 Tesla Road Speed — DI_analogSpeed

```
BO_ 872 DI_state: 8 DI
 SG_ DI_analogSpeed : 16|12@1+ (0.1,0) [0|150] "speed" NEO
```

Scale 0.1, offset 0, range 0–150 kph. Map to `"speedKmh"`.

### 1.4 CMPD is NOT the drive motor

CAN 680 (0x281) CMPD_state is the **HVAC heat pump compressor**, not the drive motor.
Evidence: CAN 641 controls the compressor; RPM range 0-20000 and current 0-50A match a compressor,
not a drive motor (0-16000+ RPM, 0-1000+ A). **Do not add CMPD signals.**

### 1.5 Current state of Model3CAN.dbc on disk

The file `resources/dbc/Model3CAN.dbc` is already the full 54KB opendbc file (uncommitted). This was
copied from `.claude/research/opendbc/` during research. **The implementing team should not commit this
file as-is.** Instead:
1. Set up the git submodule
2. Copy from `external/opendbc/opendbc/dbc/tesla_can.dbc`
3. Commit both `.gitmodules` and the DBC file

This ensures the DBC file is traceable to a specific GitHub commit.

### 1.6 isDemoMode — where it actually exists

`isDemoMode` does NOT exist in C++ production code. It exists only in the iOS bridge:

| File | Lines | What it controls |
|------|-------|-----------------|
| `VehicleSimWrapper.h` | 81 | `@property BOOL isDemoMode` — public API |
| `VehicleSimWrapper.mm` | 139,177,203,210,235,237,345-413 | Every signal getter branches on `_isDemoMode` |
| `VehicleViewModel.swift` | 17,63,75,94,115,126,135,155,203 | Gates picker, status text, buttons, calls `updateSimulator()` |
| `ContentView.swift` | 94,115,135,155 | Picker disabled, status color, demo/stop buttons |

---

## 2. Execution Sequence

Execute in this order. Each step must pass before proceeding to the next.

### Step 1 — Add opendbc git submodule

```bash
git submodule add https://github.com/commaai/opendbc.git external/opendbc
git submodule update --init --remote external/opendbc
ls external/opendbc/opendbc/dbc/tesla_can.dbc   # verify exists
ls external/opendbc/opendbc/dbc/vw_mlb.dbc        # verify exists
```

**Pitfall**: If `git submodule add` fails because `.gitmodules` already has a stub, edit
`.gitmodules` to replace the stub:
```
[submodule "external/opendbc"]
    path = external/opendbc
    url = https://github.com/commaai/opendbc.git
    branch = master
```

### Step 2 — Copy DBC files from submodule

```bash
cp external/opendbc/opendbc/dbc/tesla_can.dbc resources/dbc/Model3CAN.dbc
cp external/opendbc/opendbc/dbc/vw_mlb.dbc resources/dbc/vw_mlb.dbc
```

**Do NOT copy from `.claude/research/opendbc/`.** The submodule is the canonical GitHub-tracked source.

### Step 3 — Define gear constants in the API layer

**New file**: `include/vehicle-sim/domain/Gear.h`

```cpp
#pragma once
#include <cstdint>

namespace vehicle_sim::domain {

/**
 * Canonical gear identifiers owned by the API layer.
 *
 * Convention:
 *   Negative = special:  PARK=-2, REVERSE=-1
 *   Zero     = NEUTRAL
 *   Positive = forward gears: GEAR_1=1, GEAR_2=2, GEAR_3=3...
 *   High bit (0x1000) = AUTO mode qualifier: AUTO_1=0x1001, AUTO_2=0x1002...
 *
 * The API emits these constants. The display layer maps to labels via label().
 * If the CAN standard varies across vehicles, translation happens at the DBC
 * boundary — the API always emits the same canonical numbers.
 *
 * Tesla example: CAN DI_gear=4 (Drive, single-speed) → AUTO_1 (0x1001)
 * Manual example: CAN gear=1 (First) → GEAR_1 (1)
 * Auto example:   CAN gear=1 in auto mode → AUTO_1 (0x1001)
 */
struct Gear {
    static constexpr int32_t PARK    = -2;
    static constexpr int32_t REVERSE = -1;
    static constexpr int32_t NEUTRAL =  0;
    static constexpr int32_t GEAR_1  =  1;
    static constexpr int32_t GEAR_2  =  2;
    static constexpr int32_t GEAR_3  =  3;
    static constexpr int32_t GEAR_4  =  4;
    static constexpr int32_t GEAR_5  =  5;
    static constexpr int32_t GEAR_6  =  6;

    // AUTO mode qualifier (high bit)
    static constexpr int32_t AUTO_BIT = 0x1000;
    static constexpr int32_t AUTO_1 = AUTO_BIT | 1;  // 4097
    static constexpr int32_t AUTO_2 = AUTO_BIT | 2;  // 4098
    static constexpr int32_t AUTO_3 = AUTO_BIT | 3;  // 4099
    static constexpr int32_t AUTO_4 = AUTO_BIT | 4;
    static constexpr int32_t AUTO_5 = AUTO_BIT | 5;
    static constexpr int32_t AUTO_6 = AUTO_BIT | 6;

    /// Display label for a gear constant. Returns nullptr for unknown values.
    static const char* label(int32_t gear) noexcept {
        switch (gear) {
            case PARK:    return "P";
            case REVERSE: return "R";
            case NEUTRAL: return "N";
            case GEAR_1:  return "1";
            case GEAR_2:  return "2";
            case GEAR_3:  return "3";
            case GEAR_4:  return "4";
            case GEAR_5:  return "5";
            case GEAR_6:  return "6";
            case AUTO_1:  return "D";   // or "D1" if you want to show gear number
            case AUTO_2:  return "D2";
            case AUTO_3:  return "D3";
            case AUTO_4:  return "D4";
            case AUTO_5:  return "D5";
            case AUTO_6:  return "D6";
            default:      return nullptr;
        }
    }

    /// True if the gear constant represents an automatic mode (high bit set).
    static constexpr bool isAuto(int32_t gear) noexcept {
        return (gear & AUTO_BIT) != 0;
    }

    /// Extract the gear number from an auto gear constant.
    static constexpr int32_t gearNumber(int32_t gear) noexcept {
        return gear & ~AUTO_BIT;
    }
};

} // namespace vehicle_sim::domain
```

### Step 4 — Update DefaultVehicleConfigs.cpp

**File**: `src/domain/DefaultVehicleConfigs.cpp`

Remove `gearCodeMappings` entirely from the `VehicleConfig` struct and all constructors.
The DBC VAL_ table is the single source of truth. The `DBCSignalMapper` reads the parsed
value table from `DBCSignalDefinition` and translates raw CAN values to `Gear::` constants
at the API boundary. No config-level mapping needed.

Add signal mappings for the new signals:

```cpp
std::unordered_map<std::string, std::string>{
    {"DIR_axleSpeed",    "motorRpm"},
    {"DIR_torqueActual", "motorTorqueNm"},
    {"DI_accelPedalPos", "throttlePercent"},
    {"SteeringAngle129", "steeringAngleDeg"},
    {"DI_gear",          "gearSelector"},    // NEW — CAN 280, bits 12-14
    {"DI_analogSpeed",   "speedKmh"}          // NEW — CAN 872, bits 16-27, scale=0.1
}
```

Remove the `gearCodeMappings` parameter from the `VehicleConfig` constructor call.

### Step 5 — Update VehicleSignalFactory to use DBC VAL_ table for gear

**File**: `src/domain/VehicleSignalFactory.cpp`

Replace the special-case `if (fieldName == "gearSelector")` branch with generic value table lookup.
The `DBCSignalDefinition` already has a `valueTable` field (parsed from VAL_ entries). Use it:

```cpp
// In VehicleSignalFactory::build():
// For signals with a value table (like DI_gear), look up the raw value
// in the DBC VAL_ table and map to the canonical Gear constant.
if (fieldName == "gearSelector") {
    for (const auto& [canId, frame] : frames) {
        auto value = DBCSignalMapper::mapSignal(frame, canId, signalName,
                                                 parseResult_.signalsByCanId);
        if (value) {
            int raw = static_cast<int>(*value);
            // Translate raw CAN value to canonical Gear constant
            // using the DBC VAL_ table already parsed into DBCSignalDefinition
            gearSelector = raw;  // The API emits the raw number; display maps to label
            break;
        }
    }
    continue;
}
```

**Note**: The factory emits the **raw integer**. The display layer (CLI formatter, iOS view) calls
`Gear::label(gearSelector)` to get the display string. This keeps the API layer clean — it doesn't
know about "P" or "R", only about numbers.

### Step 6 — Build once to validate DBC parser handles the new file

```bash
make test
```

Expected: same test count as before. The DBC parser reads any valid DBC — no code changes needed
to handle 28 messages instead of 3.

### Step 7 — Dead code removal: sweep, update CMake, delete ALL dead files

**7a. Sweep for references to dead classes:**
```bash
grep -rn "CANSignalDecoderBase\|CANTranslatorBase\|AudiMLBTranslator\|TeslaCANTranslator\|TeslaSignalTranslator\|TeslaSignalParser\|SignalTranslatorFactory\|AudiSignalTranslator" \
  include/ src/ CMakeLists.txt test/CMakeLists.txt 2>&1 \
  | grep -v _deps | grep -v build | grep -v worktrees | grep -v "^Binary"
```

Expected: all hits are inside the dead files themselves or in CMakeLists. If any production file
`#includes` a dead header, that's a blocker — fix it first.

**7b. Update `CMakeLists.txt`** — Remove from `VEHICLE_SIM_LIB_SOURCES`:
```
src/domain/CANSignalDecoderBase.cpp
src/domain/CANTranslatorBase.cpp
src/domain/AudiMLBTranslator.cpp
src/domain/TeslaCANTranslator.cpp
src/domain/AudiSignalTranslator.cpp
src/domain/TeslaSignalTranslator.cpp
src/domain/TeslaSignalParser.cpp
src/domain/SignalTranslatorFactory.cpp
```
Remove from `VEHICLE_SIM_PUBLIC_HEADERS`:
```
include/vehicle-sim/domain/CANSignalDecoderBase.h
include/vehicle-sim/domain/CANTranslatorBase.h
include/vehicle-sim/domain/AudiMLBTranslator.h
include/vehicle-sim/domain/TeslaCANTranslator.h
include/vehicle-sim/domain/AudiSignalTranslator.h
include/vehicle-sim/domain/TeslaSignalTranslator.h
include/vehicle-sim/domain/TeslaSignalParser.h
include/vehicle-sim/domain/SignalTranslatorFactory.h
```

**7c. Update `test/CMakeLists.txt`** — Remove ALL dead-code test files:
```
domain/TeslaSignalParser.test.cpp
domain/SignalTranslatorFactory.test.cpp
domain/CANSignalDecoderBase.test.cpp
domain/CANTranslatorBase.test.cpp
domain/AudiMLBTranslator.test.cpp
domain/TeslaCANTranslator.test.cpp
domain/AudiSignalTranslator.test.cpp
```

**7d. Delete ALL dead files** — 9 source + 6 header + 7 test = 22 files total.

### Step 8 — Build again to confirm clean compilation

```bash
make test
```

Expected: significantly fewer tests (exact count depends on how many test cases were in the dead
test files). All remaining tests must pass.

### Step 9 — Add `make update-dbc` target to Makefile

```makefile
update-dbc:
	@echo "Updating DBC files from commaai/opendbc submodule..."
	@cd external/opendbc && git checkout master && git pull
	@cp external/opendbc/opendbc/dbc/tesla_can.dbc resources/dbc/Model3CAN.dbc
	@cp external/opendbc/opendbc/dbc/vw_mlb.dbc resources/dbc/vw_mlb.dbc
	@echo "DBC files updated."
```

**This is the ONLY supported workflow for updating DBC data.** No hand-edited DBC files.

### Step 10 — iOS: Replace embedded DBC with bundle resource

**The problem**: `VehicleSimWrapper.mm` contains the DBC as a raw string literal (`R"DBC(...)DBC"`,
lines 16-64). This is a second copy of the DBC that must be manually kept in sync. The user said:
"No scripting in iOS at all."

**The solution**: Add the DBC file to the Xcode bundle as a resource, load it at runtime via
`NSBundle`, and eliminate the embedded string literal entirely.

1. In Xcode, add `resources/dbc/Model3CAN.dbc` to the VehicleSimApp target as a **Bundle Resource**
   (not compiled, just copied into the app bundle).
2. In `VehicleSimWrapper.mm`, replace the `getEmbeddedDBC()` function:
   ```objc
   std::string getBundledDBC(const std::string& vehicleType) {
       NSString* filename = [NSString stringWithFormat:@"%@.dbc",
                              [NSString stringWithUTF8String:vehicleType.c_str()]];
       NSString* path = [[NSBundle mainBundle] pathForResource:filename ofType:nil];
       if (!path) return "";
       std::string result;
       // Read file content into result...
       return result;
   }
   ```
3. Replace the call in `connectToDevice:`:
   ```objc
   // OLD:
   std::string dbcContent = getEmbeddedDBC(vehicleTypeStr);
   // NEW:
   std::string dbcContent = getBundledDBC(vehicleTypeStr);
   ```
4. Delete the `TESLA_MODEL3_DBC` and `AUDI_MLB_DBC` string literals (lines 16-106).

**The iOS bridge now has zero DBC content.** It loads from the bundle resource, which is the same
file that the CLI reads from `resources/dbc/`. Single source of truth.

### Step 11 — iOS: Remove isDemoMode (MANDATORY — same PR)

This is **not** polish. It's an OCP violation that must be fixed in the same PR.

**The problem**: Every signal getter in `VehicleSimWrapper.mm` branches on `_isDemoMode`:
```objc
- (NSNumber *)throttlePercent {
    if (_isDemoMode.load()) {
        const auto& val = _simulator->getLatestSignal().getThrottlePercent();
        return val.has_value() ? @(val.value()) : nil;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    return _throttlePercent.has_value() ? @(_throttlePercent.value()) : nil;
}
```

**The solution**: A single `ISignalSource` C++ abstract interface, with two implementations:

```cpp
// New file: include/vehicle-sim/domain/ISignalSource.h
class ISignalSource {
public:
    virtual ~ISignalSource() = default;
    virtual vehicle_sim::domain::VehicleSignal latestSignal() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};
```

- `DemoSignalSource` wraps `DemoSignalProvider` (adapts callback to pull-based)
- `BLESignalSource` wraps the BLE callback + `DBCTranslationService`

`VehicleSimWrapper` holds a single `std::unique_ptr<ISignalSource> _signalSource` and every getter becomes:
```objc
- (NSNumber *)throttlePercent {
    auto sig = _signalSource->latestSignal();
    auto val = sig.getThrottlePercent();
    return val.has_value() ? @(val.value()) : nil;
}
```

No boolean. No branching. Adding a new data source = new implementation class, zero changes to the wrapper.

**VehicleViewModel.swift**: Remove `isDemoMode`, `startDemo()`, `stopDemo()`. Replace with a single
`connectionState` enum (`disconnected`, `connecting`, `connected`). The view model doesn't know or care
whether data comes from demo or BLE.

**ContentView.swift**: Remove `isDemoMode` checks. Remove "Start Demo" / "Stop Demo" buttons.
Replace with a single "Start" button that starts the appropriate source.

### Step 12 — Final verification

```bash
make test          # All remaining tests pass
make ios           # Xcode build succeeds
```

Acid test (tonight):
```bash
vehicle-sim --connect <BLE-ADDRESS> --vehicle tesla_model3 --log-csv tesla.csv --log-raw tesla_raw.log
```

Expected CSV columns with data:
| Column | Source |
|--------|--------|
| `throttle_pct` | CAN 280, DI_accelPedalPos |
| `speed_kmh` | **CAN 872, DI_analogSpeed** ✅ NEW |
| `brake_pct` | CAN 280, DI_brakePedalState |
| `motor_rpm` | CAN 264, DIR_axleSpeed |
| `gear_selector` | **CAN 280, DI_gear** ✅ NEW (raw integer, display maps to P/R/N/D) |
| `motor_torque_nm` | CAN 264, DIR_torqueActual |

---

## 3. Files Inventory

### New files

| File | Purpose |
|------|---------|
| `include/vehicle-sim/domain/Gear.h` | Canonical gear constants (PARK=-2, REVERSE=-1, NEUTRAL=0, GEAR_1=1, etc.) |
| `include/vehicle-sim/domain/ISignalSource.h` | Abstract interface for signal sources (demo, BLE, future) |

### Production path (KEEP, modify)

| File | Change |
|------|--------|
| `src/domain/DefaultVehicleConfigs.cpp` | Remove gearCodeMappings; add DI_gear + DI_analogSpeed mappings |
| `src/domain/VehicleSignalFactory.cpp` | Use DBC VAL_ table for gear; emit raw integer not string |
| `resources/dbc/Model3CAN.dbc` | Replace with opendbc submodule copy |
| `resources/dbc/vw_mlb.dbc` | Replace with opendbc submodule copy |
| `CMakeLists.txt` | Remove dead file references |
| `test/CMakeLists.txt` | Remove ALL dead-code test files |
| `.gitmodules` | Add opendbc submodule entry |
| `Makefile` | Add `update-dbc` target |
| `vehicle-sim-ios/VehicleSim/VehicleSimWrapper.h` | Remove `isDemoMode`, `startDemo`, `stopDemo`, `updateSimulator` |
| `vehicle-sim-ios/VehicleSim/VehicleSimWrapper.mm` | Remove embedded DBC string; remove `_isDemoMode` branching; add `ISignalSource`; use bundle resource for DBC |
| `vehicle-sim-ios/VehicleSim/VehicleViewModel.swift` | Remove `isDemoMode`; replace with `connectionState` enum |
| `vehicle-sim-ios/VehicleSim/ContentView.swift` | Remove `isDemoMode` checks; remove demo/stop buttons |

### Dead code — DELETE (22 files)

Source + header (15 files):
```
include/vehicle-sim/domain/CANSignalDecoderBase.h
src/domain/CANSignalDecoderBase.cpp
include/vehicle-sim/domain/CANTranslatorBase.h
src/domain/CANTranslatorBase.cpp
include/vehicle-sim/domain/AudiMLBTranslator.h
src/domain/AudiMLBTranslator.cpp
include/vehicle-sim/domain/TeslaCANTranslator.h
src/domain/TeslaCANTranslator.cpp
include/vehicle-sim/domain/AudiSignalTranslator.h
src/domain/AudiSignalTranslator.cpp
include/vehicle-sim/domain/TeslaSignalTranslator.h
src/domain/TeslaSignalTranslator.cpp
include/vehicle-sim/domain/TeslaSignalParser.h
src/domain/TeslaSignalParser.cpp
include/vehicle-sim/domain/SignalTranslatorFactory.h
src/domain/SignalTranslatorFactory.cpp
```

Test files (7 files):
```
test/domain/TeslaSignalParser.test.cpp
test/domain/SignalTranslatorFactory.test.cpp
test/domain/CANSignalDecoderBase.test.cpp
test/domain/CANTranslatorBase.test.cpp
test/domain/AudiMLBTranslator.test.cpp
test/domain/TeslaCANTranslator.test.cpp
test/domain/AudiSignalTranslator.test.cpp
```

---

## 4. Pitfalls

1. **`make native` corrupts project Makefile**: The `native` target runs `cd build-native && cmake ..`
   which has historically overwritten the project root Makefile. If this happens, restore with
   `git checkout Makefile` immediately.

2. **Submodule on wrong branch**: After `git submodule add`, verify it's on master:
   ```bash
   cd external/opendbc && git branch
   ```

3. **Gear constant mapping**: The DBC VAL_ table says 1=P, 2=R, 3=N, 4=D. The old code had
   `{0,"P"}, {1,"R"}, {2,"N"}, {3,"D"}, {4,"S"}` — completely wrong. Remove gearCodeMappings entirely.
   Emit raw CAN integers from the API; let the display layer map to labels via `Gear::label()`.

4. **Two DBC copies must not diverge**: After this plan, the DBC exists in exactly one place:
   `resources/dbc/Model3CAN.dbc`. The iOS bundle resource is the SAME file (added to Xcode project).
   No embedded string literal. No second copy.

5. **Do NOT copy from `.claude/research/opendbc/`**: That's a stale research snapshot. The submodule
   at `external/opendbc/` is the canonical GitHub-tracked source.

6. **Dead-code test files will fail to compile**: If you delete the source files but forget to delete
   the test files, the build will fail because the tests `#include` deleted headers. Delete ALL 7 dead
   test files BEFORE running the build.

7. **VehicleSimWrapper.mm is 516 lines**: After removing the embedded DBC string literal, the
   `_isDemoMode` branching, and the dual signal stores, it should shrink significantly. If it doesn't,
   something was missed.

---

## 5. Verification Checklist

- [ ] `git submodule add` succeeds, `external/opendbc/opendbc/dbc/tesla_can.dbc` exists
- [ ] `resources/dbc/Model3CAN.dbc` copied from submodule (not from `.claude/research/`)
- [ ] `Gear.h` created with PARK=-2, REVERSE=-1, NEUTRAL=0, GEAR_1=1, etc.
- [ ] `DefaultVehicleConfigs.cpp` has DI_gear + DI_analogSpeed mappings, no gearCodeMappings
- [ ] `VehicleSignalFactory.cpp` emits raw gear integer (not string)
- [ ] `make test` passes with new DBC
- [ ] All 22 dead files deleted (15 source/header + 7 test)
- [ ] `CMakeLists.txt` and `test/CMakeLists.txt` updated
- [ ] `make test` passes after deletion
- [ ] `make update-dbc` target added to Makefile
- [ ] iOS: Embedded DBC string literal removed from VehicleSimWrapper.mm
- [ ] iOS: DBC loaded from bundle resource via NSBundle
- [ ] iOS: `isDemoMode` removed from VehicleSimWrapper.h/.mm
- [ ] iOS: `ISignalSource` interface created; single `_signalSource` pointer replaces dual stores
- [ ] iOS: `isDemoMode` removed from VehicleViewModel.swift and ContentView.swift
- [ ] `make ios` succeeds
- [ ] Acid test: `--connect` produces gear_selector (integer) and speed_kmh columns

---

## 6. Task Breakdown (for team-lead orchestration)

Assign tasks in dependency order. Implementer and test-architect are DIFFERENT agents.

**Task 1: DBC submodule + Gear.h** (implementer)
- `git submodule add` commaai/opendbc
- Copy DBC files from submodule to `resources/dbc/`
- Create `include/vehicle-sim/domain/Gear.h` with canonical constants
- Blocked by: nothing

**Task 2: DefaultVehicleConfigs + VehicleSignalFactory** (implementer)
- Remove `gearCodeMappings` from `VehicleConfig` struct and all constructors
- Add `DI_gear` and `DI_analogSpeed` signal mappings to `teslaModel3()`
- Update `VehicleSignalFactory::build()` to use DBC VAL_ table for gear, emit raw integer
- Blocked by: nothing (can run in parallel with Task 1)

**Task 3: Dead code removal** (implementer)
- Sweep for references to dead classes
- Update `CMakeLists.txt` and `test/CMakeLists.txt`
- Delete all 22 dead files (15 source/header + 7 test)
- Blocked by: nothing (can run in parallel with Tasks 1, 2)

**Task 4: Makefile + CMake** (implementer)
- Add `make update-dbc` target
- Verify `make test` passes after all changes
- Blocked by: Tasks 1, 2, 3

**Task 5: iOS bridge refactor** (implementer)
- Remove embedded DBC string literal from `VehicleSimWrapper.mm`
- Add DBC file to Xcode bundle resource; load via `NSBundle`
- Create `ISignalSource` C++ abstract interface
- Replace `_isDemoMode` boolean + dual stores with single `_signalSource` pointer
- Remove `isDemoMode`, `startDemo`, `stopDemo`, `updateSimulator` from `VehicleSimWrapper.h`
- Remove `isDemoMode` from `VehicleViewModel.swift` and `ContentView.swift`
- Blocked by: Task 1 (needs Gear.h for signal values)

**Task 6: Critic review** (critic)
- Review ALL code from Tasks 1-5 for SOLID, DRY, separation of concerns
- Verify no `isDemoMode` boolean anywhere
- Verify no embedded DBC strings in iOS code
- Verify `gearCodeMappings` removed
- Verify `Gear.h` constants used everywhere
- Verify iOS bridge has zero domain logic
- Verify all dead-code files deleted
- Blocked by: Tasks 1, 2, 3, 4, 5

**Task 7: Write tests** (test-architect — different agent from implementer)
- Unit tests for `DefaultVehicleConfigs` (new mappings, no gearCodeMappings)
- Unit tests for `VehicleSignalFactory` (DI_gear → Gear::AUTO_1, DI_analogSpeed → speedKmh)
- Integration test: full DBC pipeline with synthetic CAN frames
- Unit tests for `Gear.h` constants and `label()` function
- Frame-replay test (automated acid test)
- Blocked by: Tasks 1, 2 (needs code to test)

**Task 8: Final verification** (team-lead)
- `make test` passes
- `make ios` succeeds
- Critic sign-off received
- All checklist items in §5 verified
- Blocked by: Tasks 4, 5, 6, 7

### Launch Instructions

```
1. Create team: "dbc-cleanup"
2. Create tasks 1-8 with dependencies above
3. Spawn 4 agents:
   - "implementer" (general-purpose) — claims Tasks 1, 2, then 3, 4, 5 in dependency order
   - "test-architect" (general-purpose) — claims Task 7 when unblocked
   - "critic" (general-purpose) — claims Task 6 when unblocked
   - "team-lead" (claude/default) — orchestrates, does NOT code
4. Team lead assigns tasks, reviews agent output, integrates
5. Critic gets final say before any code is considered done
```

---

## 7. Test Plan

### New unit tests needed

**`DefaultVehicleConfigs.test.cpp`**:
- `TeslaConfig_HasGearSignalMapping` — verify `"DI_gear"` → `"gearSelector"` in signalMappings
- `TeslaConfig_HasSpeedSignalMapping` — verify `"DI_analogSpeed"` → `"speedKmh"` in signalMappings
- `TeslaConfig_NoGearCodeMappings` — verify gearCodeMappings is empty (removed)
- `TeslaConfig_SignalMappingCount` — verify 6 entries (was 4)

**`VehicleSignalFactory.test.cpp`**:
- Update existing gear tests to use raw integers instead of strings
- `DecodesDI_GearFromCAN280` — CAN 280 frame with DI_gear=4 → gearSelector=4 (DRIVE)
- `DecodesDI_AnalogSpeedFromCAN872` — CAN 872 frame with DI_analogSpeed=500 → speedKmh≈50.0
- `GearCodeZero_ProducesNullopt` — CAN 280 with DI_gear=0 → gearSelector is nullopt
- `GearCodeSeven_ProducesNullopt` — CAN 280 with DI_gear=7 → gearSelector is nullopt

**`DBCPipelineIntegration.test.cpp`**:
- `ParseRealTeslaDBC_HasGearAndSpeedSignals` — verify DI_gear and DI_analogSpeed exist in parsed DBC
- `FullPipeline_GearAndSpeed` — feed CAN 280 + CAN 872 frames, verify gear + speed in output
- `LoadVehicleWithContent_ParsesGearAndSpeed` — verify iOS path (loadVehicleWithContent) works

**`Gear.h` (header-only, tested via usage)**:
- No separate test file needed — `Gear::label()` is tested indirectly through factory tests

### Dead-code removal verification

```bash
# Verify zero references to dead classes remain
grep -rn "CANSignalDecoderBase\|CANTranslatorBase\|AudiMLBTranslator\|TeslaCANTranslator\|TeslaSignalTranslator\|TeslaSignalParser\|SignalTranslatorFactory\|AudiSignalTranslator" \
  include/ src/ CMakeLists.txt test/CMakeLists.txt 2>&1 \
  | grep -v _deps | grep -v build | grep -v worktrees | grep -v "^Binary"
# Expected: zero output
```

### Frame-replay test (automated acid test)

Create a test fixture with synthetic CAN frames and verify the full pipeline:
```bash
# Construct known CAN frames, feed through DBCTranslationService, assert output
# This replaces the manual "acid test" with an automated equivalent
```

---

*End of plan. All paths verified, all pitfalls documented, all line numbers provided.*

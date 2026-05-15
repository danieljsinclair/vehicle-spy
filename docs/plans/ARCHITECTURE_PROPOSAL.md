# CAN Translator Architecture Proposal

> Tech Architect: Technical architect on vehicle-can-refactor team
> Date: 2026-04-30
> Based on: Code critic SOLID review, EV CAN signal research
> Worktree: agent-a04cced4
> Status: **Implemented and merged to mvp** (2026-05-15). All 5 items below are production code. motorPower/regenPower corrections applied post-merge.

---

## Summary

Proposed architecture addresses the DRY violations and SOLID issues identified by the code critic. Key changes:

1. **ITimeProvider** - Abstracts time dependency for testability (DIP)
2. **CANSignalDecoderBase** - Shared base class eliminating duplication between Audi and Tesla translators (DRY, OCP, SRP)
3. **Shared decoders** - `decodeDI280()` and `decodeSCCM297()` for common CAN IDs used by both vehicles
4. **Decoder registry** - Open/Closed compliant CAN ID dispatch via `unordered_map`
5. **VehicleSignal EV extension** - Added motorRpm, motorHvVoltage, motorHvCurrent, motorTorqueNm, gearSelector as `std::optional` fields; motorPower and regenPower were identified as bogus calculated fields and removed

---

## 1. ITimeProvider Interface

**File**: `ITimeProvider.h`

**Purpose**: Replace hardcoded `std::chrono::system_clock::now()` with injectable time source.

**Rationale (from critic review)**:
- Current code directly calls `system_clock::now()` in `buildSignal()`
- Makes testing difficult when timestamps need to be controlled
- Violates DIP: depends on concrete system time instead of abstraction

**Interface**:
```cpp
class ITimeProvider {
public:
    virtual ~ITimeProvider() = default;
    virtual uint64_t nowMs() const noexcept = 0;
};
```

**Implementations**:
- `SystemTimeProvider` - Production implementation using std::chrono
- `MockTimeProvider` - Test implementation with fixed/controlled time

---

## 2. CANSignalDecoderBase

**File**: `CANSignalDecoderBase.h`

**Purpose**: Shared base class for all CAN signal translators.

**Solves (from critic review)**:
- DRY violations: Duplicate state fields, `buildSignal()`, `extractCANId()`, constants
- OCP violation: Hardcoded CAN ID switch statements
- SRP violation: Mixed concerns (frame parsing, signal decoding, state management)

### Key Design Elements

**2.1 State Accumulation (mutable fields)**
```cpp
mutable double lastSpeedKmh_ = 0.0;
mutable double lastThrottlePercent_ = 0.0;
mutable double lastAccelerationG_ = 0.0;
mutable double lastBrakePercent_ = 0.0;
mutable double lastSteeringAngleDeg_ = 0.0;
mutable double lastMotorRpm_ = 0.0;
mutable double lastMotorHvVoltage_ = 0.0;
mutable double lastMotorHvCurrent_ = 0.0;
mutable double lastMotorTorqueNm_ = 0.0;
```

**Why mutable**: `translateFrame()` is const but accumulates state across frames.

**2.2 Decoder Registry (OCP fix)**
```cpp
using DecoderFunction = std::function<void(const std::vector<uint8_t>& data)>;
std::unordered_map<uint16_t, DecoderFunction> decoders_;
```

**Usage pattern**:
```cpp
AudiMLBTranslator::AudiMLBTranslator(std::unique_ptr<ITimeProvider> time)
    : CANSignalDecoderBase(std::move(time))
{
    registerDecoder(CAN_ID_ESP_01, [this](const auto& d) { decodeESP01(d); });
    registerDecoder(CAN_ID_DI_SYSTEM, [this](const auto& d) {
        auto [throttle, brake] = decodeDI280(d);
        lastThrottlePercent_ = throttle;
        lastBrakePercent_ = brake;
    });
    // ... more registrations
}
```

**2.3 Frame Format Constants**
```cpp
static constexpr std::size_t CAN_DATA_OFFSET = 2;
static constexpr std::size_t CAN_FRAME_SIZE = 10;
```

Shared assumption: `[canId_lo, canId_hi, data_byte_0, ..., data_byte_7]`

**2.4 Shared Decoder Methods**

**`decodeDI280()`** - CAN 280 (0x118) DI_systemStatus
- Shared by: Tesla (model3dbc), Audi (vw_mlb.dbc)
- Signals: DI_accelPedalPos (bits 32-39, scale 0.4), DI_brakePedalState (bits 17-18)
- Return: `pair<double, double>` = {acceleratorPercent, brakePercent}

**`decodeSCCM297()`** - CAN 297 (0x129) SCCM_steeringAngleSensor
- Shared by: Tesla (model3dbc), Audi (vw_mlb.dbc)
- Signal: SCCM_steeringAngle (bits 16-29, scale 0.1, offset -819.2)
- Return: `double` = steeringAngleDeg

**2.5 `buildSignal()` Method**

Uses injected `ITimeProvider::nowMs()` for timestamp and constructs `VehicleSignal`.

---

## 3. VehicleSignal EV Extension

**File**: `VehicleSignal.h`

**Fields added**:
```cpp
double motorRpm_;           // 0.0 - 20000.0 (Tesla CMPD DBC)
double motorHvVoltage;     // 0.0 - 1000.0 (Tesla CMPD DBC)
double motorHvCurrent;     // 0.0 - 50.0 (Tesla CMPD DBC)
// Note: motorPower and regenPower were removed as bogus calculated fields.
```

**Field type**: All EV and decoded fields use `std::optional<double>` — empty when the data source does not provide the value. Callers access via getters returning `const std::optional<double>&`, checking `.has_value()` before use.

---

## 4. Inheritance Pattern

**AudiMLBTranslator** (proposed):
```cpp
class AudiMLBTranslator : public CANSignalDecoderBase {
public:
    AudiMLBTranslator(std::unique_ptr<ITimeProvider> time)
        : CANSignalDecoderBase(std::move(time))
    {
        // Register Audi-specific decoders
        registerDecoder(CAN_ID_ESP_01, [this](const auto& d) { decodeESP01(d); });
        registerDecoder(CAN_ID_ESP_02, [this](const auto& d) { decodeESP02(d); });
        // ... use shared decoders for CAN 280 and 297
        registerDecoder(CAN_ID_DI_SYSTEM, [this](const auto& d) {
            auto [throttle, brake] = decodeDI280(d);
            lastThrottlePercent_ = throttle;
            lastBrakePercent_ = brake;
        });
    }

    // Vehicle-specific decode methods remain
    void decodeESP01(const std::vector<uint8_t>& data) const;
    // ...
};
```

**TeslaCANTranslator** (proposed):
```cpp
class TeslaCANTranslator : public CANSignalDecoderBase {
public:
    TeslaCANTranslator(std::unique_ptr<ITimeProvider> time)
        : CANSignalDecoderBase(std::move(time))
    {
        // Tesla currently uses only shared CAN 280 and 297
        registerDecoder(CAN_ID_DI_SYSTEM, [this](const auto& d) {
            auto [throttle, brake] = decodeDI280(d);
            lastThrottlePercent_ = throttle;
            lastBrakePercent_ = brake;
        });
        registerDecoder(CAN_ID_SCCM_STEER, [this](const auto& d) {
            lastSteeringAngleDeg_ = decodeSCCM297(d);
        });
    }
};
```

---

## 5. Open/Closed Compliance

**Adding a new vehicle type** (e.g., generic OBD2):

1. Create new class inheriting from `CANSignalDecoderBase`
2. Register CAN-specific decoders in constructor
3. **No modification required** to existing `AudiMLBTranslator` or `TeslaCANTranslator`

Example:
```cpp
class GenericOBD2Translator : public CANSignalDecoderBase {
public:
    GenericOBD2Translator(std::unique_ptr<ITimeProvider> time)
        : CANSignalDecoderBase(std::move(time))
    {
        registerDecoder(CAN_ID_OBD2_RESPONSE, [this](const auto& d) {
            decodeOBD2Response(d);
        });
    }
};
```

---

## 6. Files in This Proposal

- `include/vehicle-sim/domain/ITimeProvider.h`
- `include/vehicle-sim/domain/CANSignalDecoderBase.h`
- `include/vehicle-sim/domain/VehicleSignal.h` (EV fields added)
- `ARCHITECTURE_PROPOSAL.md` (this file)

**No implementation files (.cpp) are included per architecture rules.**

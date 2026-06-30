#include "vehicle-sim/domain/VehicleSignalFactory.h"
#include "vehicle-sim/domain/DBCSignalMapper.h"
#include "vehicle-sim/domain/VehicleSignal.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace vehicle_sim::domain {

namespace {

// Fixed output-slot layout for the numeric VehicleSignal fields. Kept in a
// single place so resolveMappings() and build() cannot drift apart.
constexpr std::size_t IDX_THROTTLE     = 0;
constexpr std::size_t IDX_SPEED        = 1;
constexpr std::size_t IDX_ACCEL        = 2;
constexpr std::size_t IDX_BRAKE        = 3;
constexpr std::size_t IDX_STEERING     = 4;
constexpr std::size_t IDX_RPM          = 5;
constexpr std::size_t IDX_HV_VOLTAGE   = 6;
constexpr std::size_t IDX_HV_CURRENT   = 7;
constexpr std::size_t IDX_TORQUE       = 8;
constexpr std::size_t IDX_GEAR         = 9; // int slot, parallel array

// Maps a VehicleSignal field name to its numeric output slot, or nullopt when
// the field is not one the factory emits. Replaces the previous build()-time
// if/else-if chain with a load-time decision (Open/Closed: the table is the
// single point of extension for new numeric fields).
[[nodiscard]] std::optional<std::size_t> numericSlotFor(std::string_view fieldName) {
    if (fieldName == "throttlePercent")   return IDX_THROTTLE;
    if (fieldName == "speedKmh")          return IDX_SPEED;
    if (fieldName == "accelerationG")     return IDX_ACCEL;
    if (fieldName == "brakePercent")      return IDX_BRAKE;
    if (fieldName == "steeringAngleDeg")  return IDX_STEERING;
    if (fieldName == "motorRpm")          return IDX_RPM;
    if (fieldName == "motorHvVoltage")    return IDX_HV_VOLTAGE;
    if (fieldName == "motorHvCurrent")    return IDX_HV_CURRENT;
    if (fieldName == "motorTorqueNm")     return IDX_TORQUE;
    return std::nullopt;
}

} // namespace

VehicleSignalFactory::VehicleSignalFactory(
    const VehicleConfig& config,
    const DBCParseResult& parseResult
) noexcept
    : config_(config)
    , parseResult_(parseResult)
{
    resolveMappings();
}

VehicleSignalFactory::LocatedSignal VehicleSignalFactory::locateSignal(
    std::string_view signalName
) const noexcept {
    for (const auto& [canId, defs] : parseResult_.signalsByCanId) {
        for (const auto& def : defs) {
            if (def.name == signalName) {
                return LocatedSignal{canId, &def};
            }
        }
    }
    return LocatedSignal{0, nullptr};
}

void VehicleSignalFactory::resolveMappings() {
    // signalMappings: signalName -> fieldName. For each, find the CAN ID +
    // definition that carries signalName. The DBC join is O(signals) once;
    // build() then pays O(mappings) per frame instead of O(mappings × frames)
    // with repeated hash probes.
    for (const auto& [signalName, fieldName] : config_.signalMappings) {
        const auto numericSlot = numericSlotFor(fieldName);
        const bool isGear = (fieldName == "gearSelector");

        if (!numericSlot.has_value() && !isGear) {
            // Mapped to a field VehicleSignal does not expose (e.g. gearRequested).
            resolved_.push_back({FieldKind::Ignored, 0, nullptr, 0});
            continue;
        }

        const auto located = locateSignal(signalName);

        // No DBC definition for this mapped signal — nothing to decode; treat
        // it as ignored (matches the previous scan, which would find no frame).
        if (located.def == nullptr) {
            resolved_.push_back({FieldKind::Ignored, 0, nullptr, 0});
            continue;
        }

        const std::size_t outIdx = isGear ? IDX_GEAR : *numericSlot;
        const FieldKind kind = isGear ? FieldKind::Gear : FieldKind::Numeric;
        resolved_.push_back({kind, located.canId, located.def, outIdx});
    }
}

VehicleSignal VehicleSignalFactory::build(
    const std::unordered_map<std::uint16_t, std::vector<std::uint8_t>>& frames,
    std::uint64_t timestampUtcMs
) const noexcept {
    // Parallel output buffers: 9 numeric slots + 1 gear slot. Defaults
    // (nullopt) match the previous per-call initialisation.
    std::array<std::optional<double>, IDX_GEAR> numeric;  // 9 numeric slots (indices 0..8)
    std::optional<std::int32_t> gear;

    for (const auto& field : resolved_) {
        if (field.kind == FieldKind::Ignored || field.def == nullptr) {
            continue;
        }

        // Direct lookup of the one frame that can carry this signal. Avoids the
        // previous all-frames scan + per-frame hash probe into signalsByCanId.
        const auto frameIt = frames.find(field.canId);
        if (frameIt == frames.end()) {
            continue;
        }

        if (field.kind == FieldKind::Gear) {
            // Reuse the existing gear decoder (value-table semantics) so output
            // stays byte-identical. The 4-arg form re-probes signalsByCanId,
            // but the definition is already known; the 2-arg mapSignal covers
            // numeric fields. Gear has no 2-arg overload, so keep the 4-arg
            // call — it is now invoked at most once per build, not per frame.
            auto gearValue = DBCSignalMapper::mapGearSignal(
                frameIt->second,
                field.canId,
                field.def->name,
                parseResult_.signalsByCanId
            );
            if (gearValue.has_value()) {
                gear = *gearValue;
            }
        } else {
            auto value = DBCSignalMapper::mapSignal(frameIt->second, *field.def);
            if (value.has_value()) {
                numeric[field.outputIndex] = *value;
            }
        }
    }

    return VehicleSignal(
        timestampUtcMs,
        numeric[IDX_THROTTLE],
        numeric[IDX_SPEED],
        numeric[IDX_ACCEL],
        numeric[IDX_BRAKE],
        numeric[IDX_STEERING],
        numeric[IDX_RPM],
        numeric[IDX_HV_VOLTAGE],
        numeric[IDX_HV_CURRENT],
        numeric[IDX_TORQUE],
        gear
    );
}

} // namespace vehicle_sim::domain

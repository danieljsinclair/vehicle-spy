#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/DBCParser.h"
#include "vehicle-sim/domain/VehicleSignal.h"

namespace vehicle_sim::domain {

/**
 * Resolves a snapshot of accumulated CAN frames into a single VehicleSignal.
 *
 * Phase 1 note: build() previously did an O(mappings × frames) nested scan on
 * EVERY call, with per-pair hash lookups into the DBC definition map. With
 * ~10 mappings and a growing frame set, that dominated replay time
 * (~1500/2400 profile samples). The definitions do not change between frames,
 * so the (fieldName → carrying canId + DBCSignalDefinition) join is now
 * precomputed once at construction; build() does one direct frame lookup +
 * one decode per field. Output is byte-identical to the previous nested-scan
 * form because each DBC signal name lives on exactly one CAN ID, so the scan
 * could only ever resolve to the one precomputed mapping.
 */
class VehicleSignalFactory final {
public:
    VehicleSignalFactory(
        const VehicleConfig& config,
        const DBCParseResult& parseResult
    ) noexcept;

    [[nodiscard]] VehicleSignal build(
        const std::unordered_map<std::uint16_t, std::vector<std::uint8_t>>& frames,
        std::uint64_t timestampUtcMs
    ) const noexcept;

private:
    /**
     * One resolved output field. A DBC signal is decoded the same way for a
     * given (canId, definition) regardless of which VehicleSignal field it
     * feeds, so the only variation is the *kind* of decoder (numeric vs gear
     * enum). Tagging the kind here (rather than re-deriving it per frame from
     * the field name) keeps build() free of the conditional ladder — the
     * policy is fixed at load time. (Open/Closed: a new decode kind is a new
     * enum value plus one branch in build(), not an edit to the field chain.)
     */
    enum class FieldKind {
        Numeric,    // scaled double signal (throttle, speed, torque, ...)
        Gear,       // gear-selector enum (value-table decode)
        Ignored,    // mapped field the VehicleSignal does not expose (e.g. gearRequested)
    };

    struct ResolvedField {
        FieldKind kind;
        std::uint16_t canId;                 // CAN ID carrying this signal (0 when Ignored)
        const DBCSignalDefinition* def;      // pre-resolved definition (nullptr when Ignored)
        std::size_t outputIndex;             // slot in build()'s output arrays
    };

    /// Join config_.signalMappings with parseResult_.signalsByCanId once.
    void resolveMappings();

    /// Result of locating a signal's owning CAN frame + definition. `def` is
    /// nullptr when no DBC definition carries `signalName`.
    struct LocatedSignal {
        std::uint16_t canId = 0;
        const DBCSignalDefinition* def = nullptr;
    };
    /// Scan parseResult_ for the single CAN ID + definition carrying
    /// `signalName`. Returns a LocatedSignal with def==nullptr when not found.
    LocatedSignal locateSignal(std::string_view signalName) const noexcept;

    const VehicleConfig& config_;
    const DBCParseResult& parseResult_;
    std::vector<ResolvedField> resolved_;
};

} // namespace vehicle_sim::domain

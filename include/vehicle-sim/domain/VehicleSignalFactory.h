#pragma once

#include <unordered_map>
#include <vector>
#include <cstdint>
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/DBCParser.h"
#include "vehicle-sim/domain/VehicleSignal.h"

namespace vehicle_sim::domain {

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
    const VehicleConfig& config_;
    const DBCParseResult& parseResult_;
};

} // namespace vehicle_sim::domain

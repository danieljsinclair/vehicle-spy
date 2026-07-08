#include "vehicle-sim/domain/DBCSignalDefinition.h"
#include "vehicle-sim/domain/DBCParser.h"

namespace vehicle_sim::domain {

DBCSignalDefinition::DBCSignalDefinition(
    const DBCSignalParams& params
) noexcept
    : canId(params.canId)
    , name(params.name)
    , startBit(params.startBit)
    , bitLength(params.bitLength)
    , byteOrder(params.byteOrder)
    , scale(params.scale)
    , offset(params.offset)
    , isSigned(params.isSigned)
    , unit(params.unit)
    , min(params.min)
    , max(params.max)
    , valueTable(params.valueTable)
{}

const std::vector<DBCSignalDefinition>* DBCParseResult::getSignalsForCanId(
    std::uint16_t canId
) const noexcept {
    auto it = signalsByCanId.find(canId);
    return it != signalsByCanId.end() ? &it->second : nullptr;
}

std::size_t DBCParseResult::totalSignalCount() const noexcept {
    std::size_t count = 0;
    for (const auto& [_, signals] : signalsByCanId) {
        count += signals.size();
    }
    return count;
}

} // namespace vehicle_sim::domain

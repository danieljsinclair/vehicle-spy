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

DBCSignalDefinition::DBCSignalDefinition(
    std::uint16_t p_canId,
    std::string p_name,
    std::size_t p_startBit,
    std::size_t p_bitLength,
    DBCByteOrder p_byteOrder,
    double p_scale,
    double p_offset,
    bool p_isSigned,
    std::string p_unit,
    double p_min,
    double p_max,
    std::vector<DBCValueEntry> p_valueTable
) noexcept
    : DBCSignalDefinition(DBCSignalParams{
        p_canId,
        std::move(p_name),
        p_startBit,
        p_bitLength,
        p_byteOrder,
        p_scale,
        p_offset,
        p_isSigned,
        std::move(p_unit),
        p_min,
        p_max,
        std::move(p_valueTable)
    })
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

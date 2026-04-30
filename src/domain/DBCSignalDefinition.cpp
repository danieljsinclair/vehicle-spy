#include "vehicle-sim/domain/DBCSignalDefinition.h"
#include "vehicle-sim/domain/DBCParser.h"

namespace vehicle_sim::domain {

DBCSignalDefinition::DBCSignalDefinition(
    std::uint16_t canId,
    std::string name,
    std::size_t startBit,
    std::size_t bitLength,
    DBCByteOrder byteOrder,
    double scale,
    double offset,
    bool isSigned,
    std::string unit,
    double min,
    double max,
    std::vector<DBCValueEntry> valueTable
) noexcept
    : canId(canId)
    , name(std::move(name))
    , startBit(startBit)
    , bitLength(bitLength)
    , byteOrder(byteOrder)
    , scale(scale)
    , offset(offset)
    , isSigned(isSigned)
    , unit(std::move(unit))
    , min(min)
    , max(max)
    , valueTable(std::move(valueTable))
{}

bool DBCSignalDefinition::operator==(
    const DBCSignalDefinition& other
) const noexcept {
    return canId == other.canId
        && name == other.name
        && startBit == other.startBit
        && bitLength == other.bitLength
        && byteOrder == other.byteOrder
        && scale == other.scale
        && offset == other.offset
        && isSigned == other.isSigned
        && unit == other.unit
        && min == other.min
        && max == other.max;
}

bool DBCSignalDefinition::operator!=(
    const DBCSignalDefinition& other
) const noexcept {
    return !(*this == other);
}

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

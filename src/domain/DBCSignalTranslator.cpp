#include "vehicle-sim/domain/DBCSignalTranslator.h"
#include "vehicle-sim/domain/VehicleSignal.h"

#include <chrono>

namespace vehicle_sim::domain {

namespace {
    constexpr std::size_t CAN_DATA_OFFSET = 2;
    constexpr std::size_t CAN_FRAME_SIZE = 10;
}

DBCSignalTranslator::DBCSignalTranslator(
    const VehicleConfig& config,
    const DBCParseResult& parseResult
) noexcept
    : factory_(config, parseResult)
    , parseResult_(parseResult)
{
}

std::optional<VehicleSignal> DBCSignalTranslator::translate(
    const std::vector<std::uint8_t>& rawData,
    std::optional<std::uint64_t> timestampUtcMs
) const noexcept {
    if (!isValidPacket(rawData)) {
        return std::nullopt;
    }

    const std::uint16_t canId = extractCANId(rawData);

    // Extract 8-byte data payload (bytes 2-9) directly into the accumulated
    // map slot, avoiding a transient vector allocation + move.
    {
        std::scoped_lock lock(frames_mutex_);
        auto& slot = accumulatedFrames_[canId];
        slot.assign(rawData.begin() + CAN_DATA_OFFSET,
                    rawData.begin() + CAN_FRAME_SIZE);
    }

    // Stamp the emitted signal with the original capture time when supplied
    // (replay path); otherwise fall back to wall-clock now() (live/BLE path).
    const std::uint64_t effectiveTs = timestampUtcMs.value_or(
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        )
    );

    // build() is a read-only consumer of the accumulated frames; hold the
    // lock across it instead of deep-copying the whole frame map per call.
    // (Previously the entire unordered_map<uint16_t,vector<uint8_t>> was copied
    // on every frame — the dominant cost of replay.) build() never calls back
    // into translate(), so this is safe and presents a consistent snapshot.
    std::scoped_lock lock(frames_mutex_);
    return factory_.build(accumulatedFrames_, effectiveTs);
}

bool DBCSignalTranslator::isValidPacket(
    const std::vector<std::uint8_t>& rawData
) const noexcept {
    return rawData.size() >= CAN_FRAME_SIZE;
}

std::vector<std::uint16_t> DBCSignalTranslator::getSupportedCANIds() const noexcept {
    std::vector<std::uint16_t> ids;
    for (const auto& [canId, _] : parseResult_.signalsByCanId) {
        ids.push_back(canId);
    }
    return ids;
}

void DBCSignalTranslator::reset() noexcept {
    std::scoped_lock lock(frames_mutex_);
    accumulatedFrames_.clear();
}

std::uint16_t DBCSignalTranslator::extractCANId(
    const std::vector<std::uint8_t>& frame
) noexcept {
    if (frame.size() < 2) return 0;
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(frame[0]) |
           (static_cast<std::uint16_t>(frame[1]) << 8));
}

} // namespace vehicle_sim::domain

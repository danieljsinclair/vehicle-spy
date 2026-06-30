#include "vehicle-sim/domain/BLESignalSource.h"
#include "vehicle-sim/domain/DBCSignalTranslator.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/BLEManager.h"

namespace vehicle_sim::domain {

BLESignalSource::BLESignalSource(BLEManager* bleManager) noexcept
    : bleManager_(bleManager)
{
}

BLESignalSource::~BLESignalSource() {
    stop();
}

VehicleSignal BLESignalSource::latestSignal() const noexcept {
    std::scoped_lock lock(signalMutex_);
    return latestSignal_;
}

void BLESignalSource::start() noexcept {
    if (connected_) return;

    bleManager_->onDataReceived([this](const std::vector<std::uint8_t>& data) {
        onDataReceived(data);
    });
    connected_ = true;
}

void BLESignalSource::stop() noexcept {
    if (!connected_) return;

    // Reset data handler
    bleManager_->onDataReceived(nullptr);
    connected_ = false;
    accumulatedFrames_.clear();
}

void BLESignalSource::onDataReceived(const std::vector<std::uint8_t>& data) {
    if (data.size() < 2) return;

    // Extract CAN ID (little-endian from BLE)
    std::uint16_t canId = static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[0]) | (static_cast<std::uint16_t>(data[1]) << 8));

    // Store frame data (bytes 2-9)
    std::vector<std::uint8_t> frameData(data.begin() + 2, data.end());

    {
        std::scoped_lock lock(signalMutex_);
        accumulatedFrames_[canId] = frameData;

        // MARKER: use DBCTranslationService to translate accumulated frames —
        // tracked separately. For now, store raw data; actual translation will
        // be added later.
    }
}

} // namespace vehicle_sim::domain
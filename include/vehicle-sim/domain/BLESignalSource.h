#pragma once

#include "vehicle-sim/domain/ISignalSource.h"
#include "vehicle-sim/BLEManager.h"
#include <mutex>
#include <unordered_map>
#include <vector>

namespace vehicle_sim::domain {

class BLESignalSource final : public ISignalSource {
public:
    explicit BLESignalSource(BLEManager* bleManager) noexcept;
    ~BLESignalSource() override;

    // Non-copyable, movable
    BLESignalSource(const BLESignalSource&) = delete;
    BLESignalSource& operator=(const BLESignalSource&) = delete;

    [[nodiscard]] VehicleSignal latestSignal() const noexcept override;
    void start() noexcept override;
    void stop() noexcept override;

private:
    void onDataReceived(const std::vector<std::uint8_t>& data);

    BLEManager* bleManager_;  // Non-owning reference - wrapper owns it
    mutable std::mutex signalMutex_;
    VehicleSignal latestSignal_{VehicleSignal::Params{.timestampUtcMs = 0}};

    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> accumulatedFrames_;
    bool connected_{false};
};

} // namespace vehicle_sim::domain
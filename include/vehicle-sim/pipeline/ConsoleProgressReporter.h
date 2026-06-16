#pragma once

#include "vehicle-sim/pipeline/IProgressReporter.h"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

namespace vehicle_sim::pipeline {

/**
 * Streaming console reporter for replay.
 *
 * Emits one newline-delimited row per decoded frame. Live capture uses this as
 * a readable console mirror of the decoded CSV schema, so the operator can see
 * every frame rather than a throttled latest-state snapshot. File replays can
 * still use it, but very large replays will produce verbose console output.
 *
 * Output is uniform across transports: runReplay() feeds it the decoded
 * VehicleSignal regardless of source, so file/tcp/ble all render identically.
 *
 * An injected ostream (default std::cout) makes the format unit-testable
 * without touching real stdout. No exceptions escape (noexcept).
 */
class ConsoleProgressReporter final : public IProgressReporter {
public:
    explicit ConsoleProgressReporter(std::ostream& out, std::string vehicleId = "") noexcept;

    void onFrame(
        const domain::VehicleSignal& signal,
        std::size_t frameIndex,
        std::size_t totalHints
    ) noexcept override;

    void onComplete(const ReplayStats& stats) noexcept override;

private:
    void emit(const domain::VehicleSignal& signal, std::size_t frameIndex, std::size_t totalHints) noexcept;

    std::ostream& out_;
    std::string vehicleId_;
    std::uint64_t lastEmittedFrame_ = 0;
};

} // namespace vehicle_sim::pipeline

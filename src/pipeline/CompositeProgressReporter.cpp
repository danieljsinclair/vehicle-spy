#include "vehicle-sim/pipeline/CompositeProgressReporter.h"

namespace vehicle_sim::pipeline {

void CompositeProgressReporter::add(IProgressReporter* reporter) {
    if (reporter) {
        reporters_.push_back(reporter);
    }
}

void CompositeProgressReporter::onFrame(
    const domain::VehicleSignal& signal,
    std::size_t frameIndex,
    std::size_t totalHints
) noexcept {
    for (auto* reporter : reporters_) {
        reporter->onFrame(signal, frameIndex, totalHints);
    }
}

void CompositeProgressReporter::onComplete(const ReplayStats& stats) noexcept {
    for (auto* reporter : reporters_) {
        reporter->onComplete(stats);
    }
}

} // namespace vehicle_sim::pipeline

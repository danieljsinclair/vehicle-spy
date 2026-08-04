#pragma once

#include "vehicle-sim/pipeline/IProgressReporter.h"

#include <cstddef>
#include <vector>

namespace vehicle_sim::pipeline {

/**
 * Fans one frame stream out to several reporters (Composite pattern).
 *
 * runReplay() accepts a single IProgressReporter, but --stdout-csv needs two
 * observers at once: the human-readable console view (on stderr) and the CSV
 * emitter (on stdout). Composing them here keeps runReplay() untouched
 * (Open/Closed) and keeps each leaf reporter single-purpose (SRP).
 *
 * Holds non-owning pointers: every child must outlive the composite. Null
 * children are ignored, so a caller can pass an optional reporter without
 * branching. Delegation order is registration order.
 */
class CompositeProgressReporter final : public IProgressReporter {
public:
    CompositeProgressReporter() = default;

    /// Register a child. Ignored when null. The child must outlive this object.
    void add(IProgressReporter* reporter);

    void onFrame(
        const domain::VehicleSignal& signal,
        std::size_t frameIndex,
        std::size_t totalHints
    ) noexcept override;

    void onComplete(const ReplayStats& stats) noexcept override;

private:
    std::vector<IProgressReporter*> reporters_;
};

} // namespace vehicle_sim::pipeline

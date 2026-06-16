#pragma once

#include "vehicle-sim/domain/VehicleSignal.h"

#include <cstddef>

namespace vehicle_sim::pipeline {

struct ReplayStats; // forward declaration (defined in PipelineReplay.h)


/**
 * Progress/telemetry observer for the replay loop.
 *
 * This is the seam that keeps console output OUT of the decoder: runReplay()
 * calls onFrame() after each successfully decoded signal and onComplete()
 * once the transport is exhausted. The same seam serves every transport — a
 * fast file replay and a live TCP/BLE stream produce identical progress
 * output because the reporter, not the transport, decides cadence.
 *
 * Implementations own their own throttling policy (e.g. emit at most one line
 * per ~150 ms of processed capture time, or every Nth frame) so a 65 k-frame
 * file replay does not flood the console while a live stream still updates
 * naturally. Open/Closed: new presentation (GUI bar, structured log) is a new
 * implementation of this interface — runReplay() never changes.
 *
 * All methods are noexcept and must never throw; a progress reporter must not
 * be able to fail the decode pipeline.
 */
class IProgressReporter {
public:
    virtual ~IProgressReporter() = default;

    IProgressReporter() = default;
    IProgressReporter(const IProgressReporter&) = delete;
    IProgressReporter& operator=(const IProgressReporter&) = delete;
    IProgressReporter(IProgressReporter&&) = delete;
    IProgressReporter& operator=(IProgressReporter&&) = delete;

    /**
     * Called once per successfully decoded frame (after the decoded sink has
     * been written). Implementations decide whether to actually emit.
     *
     * @param signal       The decoded VehicleSignal (timestamp + signals).
     * @param frameIndex   0-based index of this decoded frame within the run.
     * @param totalHints   0 if the total is unknown (live stream); otherwise a
     *                     best-effort count (e.g. file line count) for percentage.
     */
    virtual void onFrame(
        const domain::VehicleSignal& signal,
        std::size_t frameIndex,
        std::size_t totalHints
    ) noexcept = 0;

    /**
     * Called once after the transport is exhausted, with the aggregate run
     * stats. Implementations typically emit a final summary line.
     */
    virtual void onComplete(const ReplayStats& stats) noexcept = 0;
};

} // namespace vehicle_sim::pipeline

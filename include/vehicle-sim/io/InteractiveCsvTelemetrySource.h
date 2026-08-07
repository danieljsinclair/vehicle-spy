#pragma once

#include "vehicle-sim/cli/KeyboardThrottle.h"
#include "vehicle-sim/io/CsvTelemetrySource.h"
#include "vehicle-sim/util/IClock.h"

#include <memory>
#include <string>

namespace vehicle_sim::io {

/**
 * Keyboard-driven telemetry source for interactive mode.
 *
 * Polls KeyboardThrottle each tick, derives a full CsvTelemetryRow from the
 * throttle/gear/brake/steering state, and advances an internal, deterministic
 * simulated timestamp by the tick interval. The clock is injected (IClock&)
 * so the emission loop is testable without real-time sleeps: a FakeClock's
 * sleepFor advances virtual time instantly, so tests are wall-clock-free.
 */
class InteractiveCsvTelemetrySource final : public CsvTelemetrySource {
public:
    /**
     * @param throttle  Owned keyboard throttle provider.
     * @param clock     Clock used for tick pacing (non-owning; FakeClock in tests).
     * @param vehicleId Vehicle id written to every row's vehicle_id column.
     * @param intervalMs Tick interval in milliseconds (default 20 ms = 50 Hz).
     */
    InteractiveCsvTelemetrySource(
        std::unique_ptr<vehicle_sim::cli::KeyboardThrottle> throttle,
        vehicle_sim::util::IClock& clock,
        std::string vehicleId,
        int intervalMs = 20
    );

    ~InteractiveCsvTelemetrySource() override = default;

    bool hasNext() const override;
    vehicle_sim::telemetry::CsvTelemetryRow next() override;
    std::string name() const override { return "interactive"; }

private:
    std::unique_ptr<vehicle_sim::cli::KeyboardThrottle> m_throttle;
    vehicle_sim::util::IClock&                          m_clock;
    std::string                                         m_vehicleId;
    int                                                 m_intervalMs;
    std::uint64_t                                       m_timestampMs{0};
    bool                                                m_quit{false};
    bool                                                m_started{false};
};

} // namespace vehicle_sim::io

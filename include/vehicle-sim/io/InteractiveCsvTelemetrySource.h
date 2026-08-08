#pragma once

#include "vehicle-sim/interactive/CsvKeyActionTarget.h"
#include "vehicle-sim/io/CsvTelemetrySource.h"
#include "vehicle-sim/util/IClock.h"

// Keyboard mapping is owned by the engine-sim-bridge submodule.
#include "input/IKeyboardInput.h"
#include "input/KeyboardInputProvider.h"

#include <memory>
#include <string>

namespace vehicle_sim::io {

/**
 * Keyboard-driven telemetry source for interactive mode.
 *
 * Each tick drives the BRIDGE's KeyboardInputProvider one frame, reads the
 * resulting control state off the vehicle-sim callback target, derives a full
 * CsvTelemetryRow, and advances a deterministic simulated timestamp. The clock
 * is injected (IClock&) so the emission loop is testable without real-time
 * sleeps: a FakeClock's sleepFor advances virtual time instantly.
 *
 * No keyboard mapping lives here or anywhere else in vehicle-sim — key
 * decoding and hold/snap-back timing are the bridge's job. This type only
 * turns the resulting state into telemetry.
 */
class InteractiveCsvTelemetrySource final : public CsvTelemetrySource {
public:
    /**
     * @param keyboard  Raw key source (owned) — the bridge's IKeyboardInput.
     * @param clock     Clock used for tick pacing (non-owning; FakeClock in tests).
     * @param vehicleId Vehicle id written to every row's vehicle_id column.
     * @param intervalMs Tick interval in milliseconds (default 20 ms = 50 Hz).
     */
    InteractiveCsvTelemetrySource(
        std::unique_ptr<::IKeyboardInput> keyboard,
        vehicle_sim::util::IClock& clock,
        std::string vehicleId,
        int intervalMs = 20
    );

    ~InteractiveCsvTelemetrySource() override = default;

    bool hasNext() const override;
    vehicle_sim::telemetry::CsvTelemetryRow next() override;
    std::string name() const override { return "interactive"; }

private:
    // Declaration order is load-bearing: KeyboardInputProvider holds a raw
    // IKeyActionTarget* to m_target, and members are destroyed in reverse
    // declaration order, so m_target must be declared BEFORE m_provider to
    // outlive it.
    vehicle_sim::interactive::CsvKeyActionTarget        m_target;
    std::unique_ptr<input::KeyboardInputProvider>       m_provider;
    vehicle_sim::util::IClock&                          m_clock;
    std::string                                         m_vehicleId;
    int                                                 m_intervalMs;
    std::uint64_t                                       m_timestampMs{0};
    bool                                                m_quit{false};
    bool                                                m_started{false};
};

} // namespace vehicle_sim::io

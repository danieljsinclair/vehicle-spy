// InteractiveRunContext.cpp - Interactive emission loop

#include "vehicle-sim/cli/InteractiveRunContext.h"
#include "vehicle-sim/interactive/KeyboardInput.h"
#include "vehicle-sim/io/InteractiveCsvTelemetrySource.h"
#include "vehicle-sim/telemetry/CsvRowFormatter.h"

#include <iostream>
#include <memory>

namespace vehicle_sim::cli {

InteractiveRunContext::KeyboardFactory InteractiveRunContext::defaultKeyboard() {
    return []() -> std::unique_ptr<::IKeyboardInput> {
        return std::make_unique<interactive::KeyboardInput>();
    };
}

template <typename KeyboardFactoryT>
int InteractiveRunContext::runImpl(
    const std::string& vehicleId,
    int intervalMs,
    std::ostream& out,
    vehicle_sim::util::IClock& clock,
    KeyboardFactoryT makeKeyboard)
{
    using namespace vehicle_sim::telemetry;

    auto keyboard = makeKeyboard();
    if (!keyboard) {
        return 1;
    }
    // The raw key source goes straight to the telemetry source, which builds
    // the bridge's KeyboardInputProvider around it.
    auto source = std::make_unique<io::InteractiveCsvTelemetrySource>(
        std::move(keyboard),
        clock,
        vehicleId,
        intervalMs);

    out << csvHeaderLine() << "\n";

    while (source->hasNext()) {
        auto row = source->next();
        const CsvRowParams params{
            row.timestamp_ms,
            row.vehicle_id,
            std::optional<double>(row.speed_kmh),
            std::optional<double>(row.throttle_percent),
            row.brake_light,
            std::optional<double>(row.acceleration_g),
            std::optional<double>(row.steering_angle_deg),
            std::optional<double>(row.motor_rpm),
            std::optional<double>(row.motor_hv_voltage),
            std::optional<double>(row.motor_hv_current),
            std::optional<double>(row.motor_torque_nm),
            row.gear_selector,
            row.dbc_signal_count,
        };
        out << csvRowLine(params) << "\n";
        out.flush();
    }

    return 0;
}

// The only factory type used in practice is KeyboardFactory (std::function);
// both main.cpp (default factory) and the tests (fake factory) instantiate the
// template with this type, so a single explicit instantiation keeps the
// template definition out of the header.
template int InteractiveRunContext::runImpl<InteractiveRunContext::KeyboardFactory>(
    const std::string&,
    int,
    std::ostream&,
    vehicle_sim::util::IClock&,
    InteractiveRunContext::KeyboardFactory);

} // namespace vehicle_sim::cli

// InteractiveRunContext.cpp - Interactive emission loop

#include "vehicle-sim/cli/InteractiveRunContext.h"
#include "vehicle-sim/cli/KeyboardInput.h"
#include "vehicle-sim/cli/KeyboardThrottle.h"
#include "vehicle-sim/io/InteractiveCsvTelemetrySource.h"
#include "vehicle-sim/telemetry/CsvRowFormatter.h"

#include <iostream>
#include <memory>

namespace vehicle_sim::cli {

std::function<std::unique_ptr<IKeyboardInput>()> InteractiveRunContext::defaultKeyboard() {
    return []() -> std::unique_ptr<IKeyboardInput> {
        return std::make_unique<KeyboardInput>();
    };
}

int InteractiveRunContext::run(
    const std::string& vehicleId,
    int intervalMs,
    std::ostream& out,
    vehicle_sim::util::IClock& clock,
    std::function<std::unique_ptr<IKeyboardInput>()> makeKeyboard)
{
    using namespace vehicle_sim::telemetry;

    auto keyboard = makeKeyboard();
    if (!keyboard) {
        return 1;
    }
    auto throttle = std::make_unique<KeyboardThrottle>(std::move(keyboard));
    auto source = std::make_unique<io::InteractiveCsvTelemetrySource>(
        std::move(throttle),
        clock,
        vehicleId,
        intervalMs);

    out << csvHeaderLine() << "\n";

    while (source->hasNext()) {
        auto row = source->next();
        out << csvRowLine(row) << "\n";
        out.flush();
    }

    return 0;
}

} // namespace vehicle_sim::cli

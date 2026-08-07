#pragma once

#include "vehicle-sim/telemetry/CsvTelemetryRow.h"

#include <string>

namespace vehicle_sim::io {

/**
 * Abstract source of telemetry rows — consumed by replay and interactive
 * run contexts. Decouples the transport (file, keyboard, network) from the
 * emission loop.
 */
class CsvTelemetrySource {
public:
    virtual ~CsvTelemetrySource() = default;

    /** True when another row is available. */
    virtual bool hasNext() const = 0;

    /**
     * Advance to and return the next row.
     * Precondition: hasNext() is true.
     */
    virtual vehicle_sim::telemetry::CsvTelemetryRow next() = 0;

    /** Human-readable name for diagnostics (file path, "interactive", …). */
    virtual std::string name() const = 0;
};

} // namespace vehicle_sim::io

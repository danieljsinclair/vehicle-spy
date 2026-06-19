#pragma once

#include <string>
#include <memory>
#include <functional>
#include <iosfwd>
#include "vehicle-sim/domain/VehicleSignal.h"

namespace vehicle_sim::domain {
class EventDispatcher;
} // forward-declared header not needed here

namespace vehicle_sim::telemetry {

/**
 * Streaming CSV sink interface (SRP, OCP).
 *
 * Any object that can accept VehicleSignal rows and render them as CSV.
 * Enables TelemetryRunner to write CSV rows to stdout, a file, a pipe,
 * or to suppress them entirely without branching on flags inside the loop.
 */
class ICsvStdoutSink {
public:
    virtual ~ICsvStdoutSink() = default;
    virtual void operator()(const domain::VehicleSignal& signal) noexcept = 0;
};

/**
 * Concrete sink: writes CSV rows to an ostream in TraceLogger-identical format.
 * Writes a header line on construction. Each call to operator() writes one row.
 */
class CsvStdoutSink final : public ICsvStdoutSink {
public:
    explicit CsvStdoutSink(std::ostream& out);
    ~CsvStdoutSink() override = default;

    void operator()(const domain::VehicleSignal& signal) noexcept override;

    CsvStdoutSink(const CsvStdoutSink&) = delete;
    CsvStdoutSink& operator=(const CsvStdoutSink&) = delete;
    CsvStdoutSink(CsvStdoutSink&&) noexcept = default;
    CsvStdoutSink& operator=(CsvStdoutSink&&) noexcept = default;

private:
    void writeHeader();
    void writeRow(const domain::VehicleSignal& signal);
    static std::string formatOptional(std::optional<double> value);
    static std::string formatOptional(std::optional<std::int32_t> value);

    std::ostream* out_;
};

/**
 * Null-object sink: does nothing. Used when --stdout-csv is not requested.
 */
class NullCsvStdoutSink final : public ICsvStdoutSink {
public:
    NullCsvStdoutSink() = default;
    ~NullCsvStdoutSink() override = default;

    void operator()(const domain::VehicleSignal& /*signal*/) noexcept override;
};

/**
 * Factory: returns CsvStdoutSink when enabled, NullCsvStdoutSink otherwise.
 * Keeps the flag-branch out of TelemetryRunner (Open/Closed).
 */
std::unique_ptr<ICsvStdoutSink> createStdoutSink(bool enabled, std::ostream& out);

} // namespace vehicle_sim::telemetry

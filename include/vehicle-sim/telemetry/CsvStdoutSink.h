#pragma once

#include "vehicle-sim/domain/VehicleSignal.h"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>

namespace vehicle_sim::telemetry {

/**
 * Streaming CSV sink interface (SRP, OCP).
 *
 * Any object that can accept VehicleSignal rows and render them as CSV.
 * Lets a run-context emit CSV rows to stdout, a pipe, or nowhere at all
 * without branching on a flag inside the dispatch loop — the branch happens
 * once, at construction, in createStdoutSink().
 */
class ICsvStdoutSink {
public:
    virtual ~ICsvStdoutSink() = default;

    ICsvStdoutSink() = default;
    ICsvStdoutSink(const ICsvStdoutSink&) = delete;
    ICsvStdoutSink& operator=(const ICsvStdoutSink&) = delete;
    ICsvStdoutSink(ICsvStdoutSink&&) = delete;
    ICsvStdoutSink& operator=(ICsvStdoutSink&&) = delete;

    /**
     * Accept one decoded signal and render it as a CSV row.
     * Never throws — a sink must not be able to fail the decode pipeline.
     */
    virtual void operator()(const domain::VehicleSignal& signal) noexcept = 0;
};

/**
 * Concrete sink: writes CSV rows to an ostream in TraceLogger-identical
 * format (same 13-column schema, same column order, same formatting), so a
 * piped stdout stream and a `--log <base>.csv` file are byte-comparable.
 *
 * Writes the header line on construction; each operator() call writes one row.
 */
class CsvStdoutSink final : public ICsvStdoutSink {
public:
    /**
     * @param out       Stream receiving the CSV (header written immediately).
     * @param vehicleId Value for the `vehicle_id` column; "" leaves it blank.
     */
    explicit CsvStdoutSink(std::ostream& out, std::string vehicleId = "");
    ~CsvStdoutSink() override = default;

    void operator()(const domain::VehicleSignal& signal) noexcept override;

private:
    void writeHeader();
    void writeRow(const domain::VehicleSignal& signal);

    std::ostream& out_;
    std::string vehicleId_;
};

/**
 * Null-object sink: does nothing. Used when --stdout-csv is not requested,
 * so the caller holds a sink unconditionally and never tests a flag per row.
 */
class NullCsvStdoutSink final : public ICsvStdoutSink {
public:
    NullCsvStdoutSink() = default;
    ~NullCsvStdoutSink() override = default;

    void operator()(const domain::VehicleSignal& signal) noexcept override;
};

/**
 * Factory: CsvStdoutSink when enabled, NullCsvStdoutSink otherwise.
 * Keeps the flag branch out of the run-contexts (Open/Closed).
 */
std::unique_ptr<ICsvStdoutSink> createStdoutSink(bool enabled,
                                                 std::ostream& out,
                                                 const std::string& vehicleId = "");

} // namespace vehicle_sim::telemetry

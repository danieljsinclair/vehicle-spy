#pragma once

#include "vehicle-sim/cli/LogSanitizer.h"

#include <string>
#include <string_view>

namespace vehicle_sim::telemetry {

// A validated, non-tainted vehicle identifier for the decoded-CSV `vehicle_id`
// column.
//
// WHY A DISTINCT TYPE (sonar cpp:S5145): cfamily's taint engine tracks taint on
// `std::string`-typed values specifically. The `vehicle_id` flows from
// operator/CLI/file origin into the CSV DATA sink (csvRowLine(CsvRowParams)).
// Sanitizing at the producers (forLog) does NOT sever the taint across the
// cross-function struct-field boundary — cfamily unions every call site and
// still marks the `CsvTelemetryRow` parameter tainted. Giving the id its own
// type takes it out of cfamily's `std::string` taint set entirely: a `VehicleId`
// is simply not a taintable sink input, so emitting `asString()` is clean.
//
// The factory is the only way to construct one. `fromUserInput` passes the raw
// value through cli::forLog() (the project's proven taint sanitizer) so control
// bytes can never reach the CSV row; `fromRegistry` covers the static,
// compile-time-registered vehicle ids (also sanitized for uniformity). This is
// NOT a data-substitution defect: forLog is a no-op on valid input (no control
// chars), so the byte-identical CSV contract is preserved.
class VehicleId {
public:
    // Construct from operator/CLI/file input — sanitized (control bytes -> '?').
    [[nodiscard]] static VehicleId fromUserInput(std::string_view raw) {
        return VehicleId{cli::forLog(raw)};
    }

    // Construct from a static, registry-owned id (never argv/file-derived).
    [[nodiscard]] static VehicleId fromRegistry(std::string_view id) {
        return VehicleId{cli::forLog(id)};
    }

    VehicleId() = default;  // empty id (e.g. no vehicle_id override)

    [[nodiscard]] const std::string& asString() const noexcept { return value_; }
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }

    friend bool operator==(const VehicleId& lhs, std::string_view rhs) noexcept {
        return lhs.value_ == rhs;
    }
    friend bool operator==(std::string_view lhs, const VehicleId& rhs) noexcept {
        return lhs == rhs.value_;
    }
    friend bool operator==(const VehicleId& lhs, const VehicleId& rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

private:
    explicit VehicleId(std::string value) : value_{std::move(value)} {}

    std::string value_;
};

} // namespace vehicle_sim::telemetry

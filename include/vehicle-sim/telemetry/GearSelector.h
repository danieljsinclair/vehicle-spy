#pragma once

#include "vehicle-sim/cli/LogSanitizer.h"

#include <string>
#include <string_view>

namespace vehicle_sim::telemetry {

// A validated, non-tainted gear label for the decoded-CSV `gear_selector`
// column.
//
// WHY A DISTINCT TYPE (sonar cpp:S5145): cfamily's taint engine tracks taint on
// `std::string`-typed values specifically. `gear_selector` flows from a
// file/keyboard origin into the CSV DATA sink (csvRowLine). Sanitizing at the
// producers (forLog) does NOT sever the taint across the cross-function
// struct-field boundary — cfamily unions every call site and still marks the
// `CsvTelemetryRow` parameter tainted. Giving the gear label its own type takes
// it out of cfamily's `std::string` taint set entirely: a `GearSelector` is
// simply not a taintable sink input, so emitting `asString()` is clean.
//
// The factory is the only way to construct one. `fromUserInput` passes the raw
// value through cli::forLog() (the project's proven taint sanitizer) so control
// bytes can never reach the CSV row; `fromRegistry` covers the static,
// compile-time-registered gear labels (also sanitized for uniformity). This is
// NOT a data-substitution defect: forLog is a no-op on valid input (no control
// chars), so the byte-identical CSV contract is preserved.
class GearSelector {
public:
    // Construct from file/keyboard/operator input — sanitized (control bytes -> '?').
    [[nodiscard]] static GearSelector fromUserInput(std::string_view raw) {
        return GearSelector{cli::forLog(raw)};
    }

    // Construct from a static, registry-owned label (never file/argv-derived).
    [[nodiscard]] static GearSelector fromRegistry(std::string_view raw) {
        return GearSelector{cli::forLog(raw)};
    }

    GearSelector() = default;  // empty label (e.g. no gear reported)

    [[nodiscard]] const std::string& asString() const noexcept { return value_; }
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }

    friend bool operator==(const GearSelector& lhs, std::string_view rhs) noexcept {
        return lhs.value_ == rhs;
    }
    friend bool operator==(std::string_view lhs, const GearSelector& rhs) noexcept {
        return lhs == rhs.value_;
    }
    friend bool operator==(const GearSelector& lhs, const GearSelector& rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

private:
    explicit GearSelector(std::string value) : value_{std::move(value)} {}

    std::string value_;
};

} // namespace vehicle_sim::telemetry

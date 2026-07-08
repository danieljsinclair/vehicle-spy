#pragma once

#include <string>
#include "domain/TelemetrySignal.h"

namespace vehicle_sim {

enum class Format {
    JSON,
    CSV,
    PLAINTEXT
};

class TelemetryFormatter {
public:
    explicit TelemetryFormatter(Format format = Format::JSON);

    // Phase 0: Format telemetry signal from Tesla BLE data
    std::string format(const domain::TelemetrySignal& data) const;

    // Set output format
    void setFormat(Format format);

    // Enable/disable headers (for CSV)
    void setIncludeHeaders(bool include);
    void setDelimiter(char delimiter);

private:
    Format format_;
    bool include_headers_{true};
    char delimiter_{','};
};

} // namespace vehicle_sim

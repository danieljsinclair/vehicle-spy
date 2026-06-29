#include "vehicle-sim/TelemetryFormatter.h"
#include <sstream>
#include <iomanip>

namespace vehicle_sim {

TelemetryFormatter::TelemetryFormatter(Format format)
    : format_(format)
{
}

void TelemetryFormatter::setFormat(Format format) {
    format_ = format;
}

void TelemetryFormatter::setIncludeHeaders(bool include) {
    include_headers_ = include;
}

void TelemetryFormatter::setDelimiter(char delimiter) {
    delimiter_ = delimiter;
}

std::string TelemetryFormatter::format(const domain::TelemetrySignal& data) {
    std::ostringstream oss;

    switch (format_) {
        case Format::JSON: {
            oss << "{";
            oss << "\"timestamp\":" << data.getTimestampUtcMs() << ",";
            oss << "\"rpm\":" << data.getRpm() << ",";
            oss << "\"speed\":" << data.getSpeedKmh() << ",";
            oss << "\"throttle\":" << data.getThrottlePercent() << ",";
            oss << "\"torque\":" << data.getTorqueNm() << ",";
            oss << "\"gear\":" << data.getGear();
            oss << "}";
            break;
        }

        case Format::CSV: {
            if (include_headers_) {
                oss << "timestamp,rpm,speed,throttle,torque,gear" << std::endl;
            }
            oss << std::fixed << std::setprecision(6);
            oss << data.getTimestampUtcMs() << delimiter_
                << data.getRpm() << delimiter_
                << data.getSpeedKmh() << delimiter_
                << data.getThrottlePercent() << delimiter_
                << data.getTorqueNm() << delimiter_
                << data.getGear();
            break;
        }

        case Format::PLAINTEXT: {
            oss << "Telemetry:" << std::endl;
            oss << "  Time: " << data.getTimestampUtcMs() << " ms" << std::endl;
            oss << "  RPM: " << data.getRpm() << std::endl;
            oss << "  Speed: " << data.getSpeedKmh() << " km/h" << std::endl;
            oss << "  Throttle: " << data.getThrottlePercent() << "%" << std::endl;
            oss << "  Torque: " << data.getTorqueNm() << " Nm" << std::endl;
            oss << "  Gear: " << data.getGear();
            break;
        }
    }

    return oss.str();
}

} // namespace vehicle_sim

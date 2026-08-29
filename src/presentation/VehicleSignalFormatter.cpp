#include "vehicle-sim/presentation/VehicleSignalFormatter.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/VehicleDetector.h"
#include "vehicle-sim/domain/Gear.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace vehicle_sim::presentation {

std::string formatTelemetryRow(const domain::VehicleSignal& signal, int count) {
    std::ostringstream out;
    printTelemetryRow(out, signal, count);
    return out.str();
}

std::string formatTelemetryHeader(const domain::VehicleConfig& config) {
    std::ostringstream out;
    printTelemetryHeader(out, config);
    return out.str();
}

void printTelemetryRow(std::ostream& out, const domain::VehicleSignal& signal, int count) {
    out << "[" << count << "] ";
    out << "Throttle: " << std::setw(5) << std::fixed << std::setprecision(1)
        << signal.getThrottlePercent().value_or(0.0) << "%  ";
    out << "Speed: " << std::setw(5) << std::fixed << std::setprecision(1)
        << signal.getSpeedKmh().value_or(0.0) << " km/h  ";
    out << "Brake: " << std::setw(5) << std::fixed << std::setprecision(1)
        << signal.getBrakePercent().value_or(0.0) << "%  ";
    out << "Accel: " << std::setw(5) << std::fixed << std::setprecision(2)
        << signal.getAccelerationG().value_or(0.0) << " G  ";

    auto gearVal = signal.getGearSelector();
    out << "Gear: " << std::setw(1)
        << (gearVal ? domain::Gear::labelOr(*gearVal, "?") : "-") << "  ";
    out << "Steer: " << std::setw(6) << std::fixed << std::setprecision(1)
        << signal.getSteeringAngleDeg().value_or(0.0) << "°  ";
    out << "Motor: " << std::setw(5) << std::fixed << std::setprecision(0)
        << signal.getMotorRpm().value_or(0.0) << " rpm  ";
    out << "HV: " << std::setw(5) << std::fixed << std::setprecision(1)
        << signal.getMotorHvVoltage().value_or(0.0) << "V  ";
    out << "Curr: " << std::setw(5) << std::fixed << std::setprecision(1)
        << signal.getMotorHvCurrent().value_or(0.0) << "A  ";
    out << "Trq: " << std::setw(6) << std::fixed << std::setprecision(1)
        << signal.getMotorTorqueNm().value_or(0.0) << " Nm\n";
}

void printTelemetryHeader(std::ostream& out, const domain::VehicleConfig& config) {
    out << "\n" << std::string(TERMINAL_SEPARATOR_WIDTH, '=') << "\n";
    out << config.vehicleName << " Real-Time Telemetry\n";
    out << std::string(TERMINAL_SEPARATOR_WIDTH, '=') << "\n\n";
}

namespace {

// Confidence wording for the detection summary. Mirrors DetectionConfidence.
const char* confidenceLabel(domain::DetectionConfidence confidence) noexcept {
    switch (confidence) {
        case domain::DetectionConfidence::High: return "high";
        case domain::DetectionConfidence::Medium: return "medium";
        case domain::DetectionConfidence::Low: return "low";
        case domain::DetectionConfidence::None: return "none";
    }
    return "none";
}

} // namespace

std::string formatDetectionSummary(const domain::VehicleDetectionResult& result) {
    if (result.frameCount == 0) {
        return "";
    }

    std::ostringstream out;
    out << "Frames: " << result.frameCount;

    if (!result.observedCanIds.empty()) {
        // The detector collects IDs in an unordered set; sort for a
        // deterministic summary line.
        std::vector<std::uint16_t> ids(result.observedCanIds.begin(),
                                       result.observedCanIds.end());
        std::sort(ids.begin(), ids.end());
        out << " | CAN IDs:";
        for (const std::uint16_t id : ids) {
            out << " 0x" << std::hex << std::uppercase << std::setw(4)
                << std::setfill('0') << id << std::dec;
        }
    }

    if (result.hasSuggestion()) {
        out << " | " << result.suggestedVehicleId
            << " (" << confidenceLabel(result.confidence) << ")";
    }

    return out.str();
}

} // namespace vehicle_sim::presentation

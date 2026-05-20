#include "vehicle-sim/presentation/VehicleSignalFormatter.h"
#include "vehicle-sim/domain/VehicleSignal.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/Gear.h"

#include <iomanip>
#include <sstream>
#include <string>

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
        << (gearVal ? (domain::Gear::label(*gearVal) ? domain::Gear::label(*gearVal) : "?") : "-") << "  ";
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

} // namespace vehicle_sim::presentation

#pragma once

#include <stdexcept>
#include <string>
#include <vector>
#include <sstream>

namespace vehicle_sim::domain {

/**
 * Base exception for vehicle simulation domain errors
 */
class VehicleSimException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * Exception thrown when a vehicle configuration is not found
 */
class VehicleConfigNotFoundException : public VehicleSimException {
public:
    VehicleConfigNotFoundException(const std::string& vehicleType, const std::vector<std::string>& availableVehicles)
        : VehicleSimException(buildMessage(vehicleType, availableVehicles))
        , vehicleType_(vehicleType)
        , availableVehicles_(availableVehicles) {}

    [[nodiscard]] const std::string& vehicleType() const noexcept {
        return vehicleType_;
    }

    [[nodiscard]] const std::vector<std::string>& availableVehicles() const noexcept {
        return availableVehicles_;
    }

private:
    static std::string buildMessage(const std::string& vehicleType, const std::vector<std::string>& availableVehicles) {
        std::ostringstream oss;
        oss << "Vehicle config not found: " << vehicleType << "\n";
        oss << "Available vehicles: ";
        for (const auto& v : availableVehicles) {
            oss << v << " ";
        }
        return oss.str();
    }

    std::string vehicleType_;
    std::vector<std::string> availableVehicles_;
};

/**
 * Exception thrown when DBC loading fails for a vehicle
 */
class DBCLoadException : public VehicleSimException {
public:
    explicit DBCLoadException(const std::string& vehicleType)
        : VehicleSimException("Failed to load DBC for vehicle: " + vehicleType)
        , vehicleType_(vehicleType) {}

    [[nodiscard]] const std::string& vehicleType() const noexcept {
        return vehicleType_;
    }

private:
    std::string vehicleType_;
};

/**
 * Exception thrown when telemetry file cannot be opened
 */
class TelemetryFileException : public VehicleSimException {
public:
    explicit TelemetryFileException(const std::string& filePath)
        : VehicleSimException("Failed to open telemetry file: " + filePath)
        , filePath_(filePath) {}

    [[nodiscard]] const std::string& filePath() const noexcept {
        return filePath_;
    }

private:
    std::string filePath_;
};

} // namespace vehicle_sim::domain
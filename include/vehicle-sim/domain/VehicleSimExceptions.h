#pragma once

#include <stdexcept>
#include <string>
#include <vector>
#include <sstream>

#include "vehicle-sim/cli/LogSanitizer.h"

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
        oss << "Vehicle config not found: " << cli::forLog(vehicleType) << "\n";
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
 *
 * Self-diagnosing: the message names the vehicle, the load stage that failed
 * ("open" = the DBC file could not be opened, "zero-signals" = it parsed to
 * zero signals, "unknown-vehicle" = id not in the registry), the declared DBC
 * resource path, and every concrete path resolution tried.
 */
class DBCLoadException : public VehicleSimException {
public:
    DBCLoadException(const std::string& vehicleType,
                     const std::string& stage,
                     const std::string& resourcePath,
                     const std::string& resolvedPath,
                     const std::vector<std::string>& pathsTried)
        : VehicleSimException(buildMessage(vehicleType, stage, resourcePath, resolvedPath, pathsTried))
        , vehicleType_(vehicleType)
        , stage_(stage)
        , pathsTried_(pathsTried) {}

    [[nodiscard]] const std::string& vehicleType() const noexcept {
        return vehicleType_;
    }

    [[nodiscard]] const std::string& stage() const noexcept {
        return stage_;
    }

    [[nodiscard]] const std::vector<std::string>& pathsTried() const noexcept {
        return pathsTried_;
    }

private:
    static std::string buildMessage(const std::string& vehicleType,
                                    const std::string& stage,
                                    const std::string& resourcePath,
                                    const std::string& resolvedPath,
                                    const std::vector<std::string>& pathsTried) {
        std::ostringstream oss;
        oss << "Failed to load DBC for vehicle: " << cli::forLog(vehicleType) << "\n";
        oss << "Failed stage: " << stage << "\n";
        oss << "DBC resource: " << (resourcePath.empty() ? "(none)" : resourcePath) << "\n";
        oss << "Resolved DBC path: " << (resolvedPath.empty() ? "(none)" : resolvedPath) << "\n";
        if (pathsTried.empty()) {
            oss << "Paths tried: (none)\n";
        } else {
            oss << "Paths tried:\n";
            for (const auto& path : pathsTried) {
                oss << "  - " << path << "\n";
            }
        }
        return oss.str();
    }

    std::string vehicleType_;
    std::string stage_;
    std::vector<std::string> pathsTried_;
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
#pragma once

#include "vehicle-sim/domain/ISignalSource.h"
#include "vehicle-sim/cli/CliOptions.h"
#include <memory>
#include <string>

namespace vehicle_sim::domain {

/**
 * Factory for creating ISignalSource implementations
 *
 * DI: Factory pattern for OCP compliance - new sources don't require
 * modifying main() or other factory functions.
 */
class SignalSourceFactory {
public:
    /**
     * Create a signal source based on CLI options
     *
     * @param sourceType "demo" or "ble"
     * @param updateIntervalMs Update interval for demo source
     * @return Unique pointer to ISignalSource
     * @throws std::invalid_argument if source type is unknown
     */
    [[nodiscard]] static std::unique_ptr<ISignalSource> create(
        const std::string& sourceType,
        int updateIntervalMs
    );
};

} // namespace vehicle_sim::domain
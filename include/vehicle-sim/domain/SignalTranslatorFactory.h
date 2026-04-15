#pragma once

#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include "vehicle-sim/domain/ISignalTranslator.h"

namespace vehicle_sim::domain {

/**
 * Factory for creating ISignalTranslator instances by vehicle type.
 *
 * DI-friendly: inject this factory wherever translator selection is needed.
 * Supports "generic" (standard OBD2) and "tesla" vehicle types.
 * Unknown types fall back to generic OBD2.
 */
class SignalTranslatorFactory {
public:
    /**
     * Create a signal translator for the given vehicle type.
     * @param vehicleType "generic", "tesla", or empty (defaults to generic)
     * @return Unique pointer to the appropriate ISignalTranslator
     */
    [[nodiscard]] std::unique_ptr<ISignalTranslator> create(
        const std::string& vehicleType
    ) const;

    /**
     * Get list of supported vehicle types.
     * @return Vector of type names
     */
    [[nodiscard]] static std::vector<std::string> availableTypes();
};

} // namespace vehicle_sim::domain

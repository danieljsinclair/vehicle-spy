#pragma once

#include "vehicle-sim/domain/IDBCContentProvider.h"
#include <string>

namespace vehicle_sim::domain {

/**
 * iOS-specific DBC content provider that reads from NSBundle.
 * The Obj-C bridge provides the bundle path reading.
 */
class IOSDBCContentProvider : public IDBCContentProvider {
public:
    /**
     * Load DBC content from iOS bundle.
     * @param fileName The DBC filename (e.g., "Model3CAN.dbc")
     * @return DBC file contents, or empty string if not found
     */
    [[nodiscard]] std::string loadContent(const std::string& fileName) const override;
};

} // namespace vehicle_sim::domain
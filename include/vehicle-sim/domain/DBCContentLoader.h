#pragma once

#include <string>
#include <functional>
#include <memory>

namespace vehicle_sim::domain {

/**
 * Function type for loading DBC content from a bundle.
 * The iOS bridge provides this to load from NSBundle, tests provide synthetic content.
 */
using DBCContentLoader = std::function<std::string(const std::string&)>;

} // namespace vehicle_sim::domain
#include "vehicle-sim/domain/SignalTranslatorFactory.h"
#include "vehicle-sim/domain/OBD2SignalTranslator.h"
#include "vehicle-sim/domain/TeslaSignalTranslator.h"
#include "vehicle-sim/domain/AudiSignalTranslator.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace vehicle_sim::domain {

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

} // anonymous namespace

std::unique_ptr<ISignalTranslator> SignalTranslatorFactory::create(
    const std::string& vehicleType
) const {
    const std::string lower = toLower(vehicleType);

    if (lower == "tesla") {
        return std::make_unique<TeslaSignalTranslator>();
    }
    if (lower == "audi") {
        return std::make_unique<AudiSignalTranslator>();
    }

    // Default: generic OBD2 (covers "generic", empty string, unknown types)
    return std::make_unique<OBD2SignalTranslator>();
}

std::vector<std::string> SignalTranslatorFactory::availableTypes() {
    return {"generic", "tesla", "audi"};
}

} // namespace vehicle_sim::domain

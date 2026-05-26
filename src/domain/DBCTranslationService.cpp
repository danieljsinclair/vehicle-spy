#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DBCFileParser.h"
#include "vehicle-sim/domain/DBCSignalTranslator.h"
#include "vehicle-sim/domain/OBD2SignalTranslator.h"
#include "vehicle-sim/domain/ISignalTranslator.h"

namespace vehicle_sim::domain {

class DBCTranslationService::Impl {
public:
    VehicleConfigRegistry registry_;
    DBCFileParser parser_;
    DBCParseResult parseResult_;
    std::unique_ptr<ISignalTranslator> translator_;
    VehicleProtocol protocol_{VehicleProtocol::Simulation};
    std::string vehicleId_;
    bool loaded_{false};
};

DBCTranslationService::DBCTranslationService()
    : pImpl(std::make_unique<Impl>())
{
}

DBCTranslationService::~DBCTranslationService() = default;

bool DBCTranslationService::loadVehicle(const std::string& vehicleId, VehicleProtocol protocol) {
    const VehicleConfig* config = pImpl->registry_.getConfig(vehicleId);

    // Use config's isCANProtocol to determine path, unless caller explicitly overrides
    const bool useCAN = (protocol == VehicleProtocol::CAN) ||
                        (config && config->isCANProtocol && protocol != VehicleProtocol::OBD2);

    if (useCAN) {
        if (!config) {
            return false;
        }
        pImpl->parseResult_ = pImpl->parser_.parseFile(config->dbcFilePath);
        if (pImpl->parseResult_.signalsByCanId.empty()) {
            return false;
        }
        pImpl->translator_ = std::make_unique<DBCSignalTranslator>(*config, pImpl->parseResult_);
        pImpl->protocol_ = VehicleProtocol::CAN;
    } else if (protocol == VehicleProtocol::OBD2 || !config || !config->isCANProtocol) {
        pImpl->translator_ = std::make_unique<OBD2SignalTranslator>();
        pImpl->protocol_ = VehicleProtocol::OBD2;
    } else {
        pImpl->translator_ = nullptr;
        pImpl->protocol_ = protocol;
    }

    pImpl->loaded_ = true;
    pImpl->vehicleId_ = vehicleId;

    return true;
}

bool DBCTranslationService::loadVehicleWithContent(const std::string& vehicleId, VehicleProtocol protocol, const std::string& dbcContent) {
    const VehicleConfig* config = pImpl->registry_.getConfig(vehicleId);

    const bool useCAN = (protocol == VehicleProtocol::CAN) ||
                        (config && config->isCANProtocol && protocol != VehicleProtocol::OBD2);

    if (useCAN) {
        if (!config) {
            return false;
        }
        pImpl->parseResult_ = pImpl->parser_.parseString(dbcContent);
        if (pImpl->parseResult_.signalsByCanId.empty()) {
            return false;
        }
        pImpl->translator_ = std::make_unique<DBCSignalTranslator>(*config, pImpl->parseResult_);
        pImpl->protocol_ = VehicleProtocol::CAN;
    } else if (protocol == VehicleProtocol::OBD2 || !config || !config->isCANProtocol) {
        pImpl->translator_ = std::make_unique<OBD2SignalTranslator>();
        pImpl->protocol_ = VehicleProtocol::OBD2;
    } else {
        pImpl->translator_ = nullptr;
        pImpl->protocol_ = protocol;
    }

    pImpl->loaded_ = true;
    pImpl->vehicleId_ = vehicleId;

    return true;
}

bool DBCTranslationService::loadVehicleFromPath(const std::string& vehicleId, VehicleProtocol protocol, const std::string& dbcAbsolutePath) {
    const VehicleConfig* config = pImpl->registry_.getConfig(vehicleId);

    const bool useCAN = (protocol == VehicleProtocol::CAN) ||
                        (config && config->isCANProtocol && protocol != VehicleProtocol::OBD2);

    if (useCAN) {
        if (!config) {
            return false;
        }
        pImpl->parseResult_ = pImpl->parser_.parseFile(dbcAbsolutePath);
        if (pImpl->parseResult_.signalsByCanId.empty()) {
            return false;
        }
        pImpl->translator_ = std::make_unique<DBCSignalTranslator>(*config, pImpl->parseResult_);
        pImpl->protocol_ = VehicleProtocol::CAN;
    } else if (protocol == VehicleProtocol::OBD2 || !config || !config->isCANProtocol) {
        pImpl->translator_ = std::make_unique<OBD2SignalTranslator>();
        pImpl->protocol_ = VehicleProtocol::OBD2;
    } else {
        pImpl->translator_ = nullptr;
        pImpl->protocol_ = protocol;
    }

    pImpl->loaded_ = true;
    pImpl->vehicleId_ = vehicleId;

    return true;
}

std::optional<VehicleSignal> DBCTranslationService::processFrame(const std::vector<std::uint8_t>& rawData) const noexcept {
    if (!pImpl->loaded_ || !pImpl->translator_) {
        return std::nullopt;
    }

    return pImpl->translator_->translate(rawData);
}

VehicleProtocol DBCTranslationService::getProtocol() const noexcept {
    return pImpl->protocol_;
}

std::string DBCTranslationService::getVehicleId() const noexcept {
    return pImpl->vehicleId_;
}

bool DBCTranslationService::isLoaded() const noexcept {
    return pImpl->loaded_;
}

VehicleConfigRegistry& DBCTranslationService::registry() {
    return pImpl->registry_;
}

const VehicleConfigRegistry& DBCTranslationService::registry() const {
    return pImpl->registry_;
}

} // namespace vehicle_sim::domain

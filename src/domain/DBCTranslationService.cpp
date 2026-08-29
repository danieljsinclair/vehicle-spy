#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DBCFileParser.h"
#include "vehicle-sim/domain/DBCSignalTranslator.h"
#include "vehicle-sim/domain/OBD2SignalTranslator.h"
#include "vehicle-sim/domain/ISignalTranslator.h"
#include "vehicle-sim/util/ExecutablePath.h"

#include <fstream>
#include <string_view>

namespace vehicle_sim::domain {

namespace {

// True when `path` can be opened for reading — distinguishes an unopenable
// (missing/unreadable) DBC from one that parses to zero signals.
bool fileOpenable(const std::string& path) noexcept {
    std::ifstream file(path);
    return file.good();
}

} // namespace

class DBCTranslationService::Impl {
public:
    VehicleConfigRegistry registry_;
    DBCFileParser parser_;
    DBCParseResult parseResult_;
    std::unique_ptr<ISignalTranslator> translator_;
    VehicleProtocol protocol_{VehicleProtocol::Simulation};
    std::string vehicleId_;
    bool loaded_{false};
    DBCTranslationService::DBCLoadFailure lastFailure_;

    void resetFailure(std::string_view vehicleId) {
        lastFailure_ = DBCLoadFailure{};
        lastFailure_.vehicleId = vehicleId;
    }

    void recordFailure(DBCLoadFailure::Stage stage,
                       const std::string& vehicleId,
                       const std::string& resourcePath,
                       const std::string& resolvedPath,
                       std::vector<std::string> candidatesTried) {
        lastFailure_ = DBCLoadFailure{stage, vehicleId, resourcePath, resolvedPath,
                                      std::move(candidatesTried)};
    }
};

const char* DBCTranslationService::DBCLoadFailure::stageLabel() const noexcept {
    switch (stage) {
        case Stage::None:           return "none";
        case Stage::UnknownVehicle: return "unknown-vehicle";
        case Stage::Open:           return "open";
        case Stage::ZeroSignals:    return "zero-signals";
    }
    return "none";
}

DBCTranslationService::DBCTranslationService()
    : pImpl(std::make_unique<Impl>())
{
}

DBCTranslationService::~DBCTranslationService() = default;

bool DBCTranslationService::loadVehicle(const std::string& vehicleId, VehicleProtocol protocol) {
    const VehicleConfig* config = pImpl->registry_.getConfig(vehicleId);
    pImpl->resetFailure(vehicleId);

    // Use config's isCANProtocol to determine path, unless caller explicitly overrides
    if (const bool useCAN = (protocol == VehicleProtocol::CAN) ||
                            (config && config->isCANProtocol && protocol != VehicleProtocol::OBD2);
        useCAN) {
        if (!config) {
            pImpl->recordFailure(DBCLoadFailure::Stage::UnknownVehicle, vehicleId, "", "", {});
            return false;
        }
        // Resolve the DBC path relative to the running executable so the binary
        // works from any CWD (config->dbcFilePath is a PWD-relative default).
        const std::string dbcPath =
            util::ExecutablePath::resolveResource(config->dbcFilePath);
        pImpl->parseResult_ = pImpl->parser_.parseFile(dbcPath);
        if (pImpl->parseResult_.signalsByCanId.empty()) {
            const DBCLoadFailure::Stage stage =
                fileOpenable(dbcPath) ? DBCLoadFailure::Stage::ZeroSignals
                                      : DBCLoadFailure::Stage::Open;
            pImpl->recordFailure(stage, vehicleId, config->dbcFilePath, dbcPath,
                                 util::ExecutablePath::resourceCandidates(config->dbcFilePath));
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
    pImpl->resetFailure(vehicleId);

    if (const bool useCAN = (protocol == VehicleProtocol::CAN) ||
                            (config && config->isCANProtocol && protocol != VehicleProtocol::OBD2);
        useCAN) {
        if (!config) {
            pImpl->recordFailure(DBCLoadFailure::Stage::UnknownVehicle, vehicleId, "", "", {});
            return false;
        }
        pImpl->parseResult_ = pImpl->parser_.parseString(dbcContent);
        if (pImpl->parseResult_.signalsByCanId.empty()) {
            // No file involved: the supplied content parsed to zero signals.
            pImpl->recordFailure(DBCLoadFailure::Stage::ZeroSignals, vehicleId,
                                 "<inline DBC content>", "", {});
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
    pImpl->resetFailure(vehicleId);

    if (const bool useCAN = (protocol == VehicleProtocol::CAN) ||
                            (config && config->isCANProtocol && protocol != VehicleProtocol::OBD2);
        useCAN) {
        if (!config) {
            pImpl->recordFailure(DBCLoadFailure::Stage::UnknownVehicle, vehicleId, "", "", {});
            return false;
        }
        const std::string dbcPath = util::ExecutablePath::resolveResource(dbcAbsolutePath);
        pImpl->parseResult_ = pImpl->parser_.parseFile(dbcPath);
        if (pImpl->parseResult_.signalsByCanId.empty()) {
            const DBCLoadFailure::Stage stage =
                fileOpenable(dbcPath) ? DBCLoadFailure::Stage::ZeroSignals
                                      : DBCLoadFailure::Stage::Open;
            pImpl->recordFailure(stage, vehicleId, dbcAbsolutePath, dbcPath,
                                 util::ExecutablePath::resourceCandidates(dbcAbsolutePath));
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

std::optional<VehicleSignal> DBCTranslationService::processFrame(
    const std::vector<std::uint8_t>& rawData,
    std::optional<std::uint64_t> timestampUtcMs
) const noexcept {
    if (!pImpl->loaded_ || !pImpl->translator_) {
        return std::nullopt;
    }

    return pImpl->translator_->translate(rawData, timestampUtcMs);
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

const DBCTranslationService::DBCLoadFailure& DBCTranslationService::lastLoadFailure() const noexcept {
    return pImpl->lastFailure_;
}

VehicleConfigRegistry& DBCTranslationService::registry() {
    return pImpl->registry_;
}

const VehicleConfigRegistry& DBCTranslationService::registry() const {
    return pImpl->registry_;
}

} // namespace vehicle_sim::domain

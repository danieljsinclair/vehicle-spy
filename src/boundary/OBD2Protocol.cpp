#include "vehicle-sim/boundary/OBD2Protocol.h"

#include <thread>
#include <chrono>
#include <string_view>

namespace vehicle_sim::boundary {

OBD2Protocol::OBD2Protocol() = default;

void OBD2Protocol::setSendCallback(SendCallback callback) {
    sendCallback_ = std::move(callback);
}

void OBD2Protocol::processIncomingData(std::string_view asciiData) {
    auto binaryData = ELM327Transport::parseOBD2Response(std::string(asciiData));
    if (!binaryData) {
        return;
    }

    // Try to feed as VIN response
    if (detector_.feedVINResponse(*binaryData)) {
        return;
    }

    // Try to feed as fuel type response
    detector_.feedFuelTypeResponse(*binaryData);
}

void OBD2Protocol::initialize() const {
    if (!sendCallback_) {
        return;
    }

    auto initCommands = ELM327Transport::buildInitSequence();

    for (const auto& cmd : initCommands) {
        sendCallback_(cmd.command);

        if (cmd.delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(cmd.delayMs));
        }
    }
}

std::optional<domain::VehicleDetectionResult> OBD2Protocol::detectVehicle() {
    detector_.reset();

    // Send VIN query
    if (sendCallback_) {
        sendCallback_(buildVINQuery());
    }

    // In a real implementation, we would wait for the response here
    // For now, return the current detector state
    // The caller is responsible for calling processIncomingData() with the responses
    return detector_.getResult();
}

std::string OBD2Protocol::buildVINQuery() {
    return ELM327Transport::buildOBD2Query(0x09, 0x02);
}

std::string OBD2Protocol::buildFuelTypeQuery() {
    return ELM327Transport::buildOBD2Query(0x01, 0x51);
}

} // namespace vehicle_sim::boundary

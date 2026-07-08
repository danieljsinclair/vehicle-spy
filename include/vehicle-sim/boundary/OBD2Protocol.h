#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <optional>
#include <functional>
#include "vehicle-sim/boundary/ELM327Transport.h"
#include "vehicle-sim/domain/VehicleDetector.h"

namespace vehicle_sim::boundary {

/**
 * @brief High-level OBD2 protocol handler using ELM327 transport.
 *
 * Combines ELM327Transport (ASCII encoding/decoding) with VehicleDetector
 * to provide automatic vehicle detection and OBD2 query functionality.
 *
 * Usage:
 *   OBD2Protocol protocol;
 *   protocol.initialize(sendCallback);
 *   auto detected = protocol.detectVehicle();
 *   if (detected) {
 *       translationService.loadVehicle(detected->suggestedVehicleId, VehicleProtocol::OBD2);
 *   }
 */
class OBD2Protocol {
public:
    using SendCallback = std::function<void(std::string_view asciiCommand)>;

    OBD2Protocol();
    ~OBD2Protocol() = default;

    /**
     * @brief Set the callback for sending ASCII commands to the BLE adapter.
     * @param callback Function that receives ASCII commands to send
     */
    void setSendCallback(SendCallback callback);

    /**
     * @brief Process incoming ASCII data from ELM327 adapter.
     * @param asciiData Raw ASCII response from adapter
     */
    void processIncomingData(std::string_view asciiData);

    /**
     * @brief Send ELM327 initialization sequence.
     * Calls sendCallback for each AT command with appropriate delays.
     */
    void initialize() const;

    /**
     * @brief Detect vehicle by querying VIN and fuel type.
     * @return Detection result if successful, nullopt otherwise
     */
    std::optional<domain::VehicleDetectionResult> detectVehicle();

    /**
     * @brief Build VIN query command (Mode 09 PID 02).
     * @return ASCII command string
     */
    static std::string buildVINQuery();

    /**
     * @brief Build fuel type query command (Mode 01 PID 51).
     * @return ASCII command string
     */
    static std::string buildFuelTypeQuery();

private:
    domain::VehicleDetector detector_;
    SendCallback sendCallback_;

    std::optional<std::vector<uint8_t>> parseAndFeedVIN(const std::string& response);
    std::optional<std::vector<uint8_t>> parseAndFeedFuelType(const std::string& response);
};

} // namespace vehicle_sim::boundary

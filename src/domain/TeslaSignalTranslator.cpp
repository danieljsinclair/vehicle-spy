#include "vehicle-sim/domain/TeslaSignalTranslator.h"

#include <numeric>

namespace vehicle_sim::domain {

bool TeslaSignalTranslator::isValidPacket(
    const std::vector<std::uint8_t>& rawData
) const noexcept
{
    // Check minimum packet size
    if (rawData.size() < MIN_PACKET_SIZE) {
        return false;
    }

    // Check header bytes
    if (rawData[0] != HEADER_0 || rawData[1] != HEADER_1) {
        return false;
    }

    // Check payload length matches
    const std::uint8_t payloadLength = rawData[2];
    const std::size_t expectedTotalSize = PAYLOAD_START + payloadLength + 1; // +1 for checksum
    if (rawData.size() != expectedTotalSize) {
        return false;
    }

    // Verify checksum
    const std::uint8_t expectedChecksum = calculateChecksum(rawData);
    const std::uint8_t actualChecksum = rawData.back();

    return expectedChecksum == actualChecksum;
}

std::optional<VehicleSignal> TeslaSignalTranslator::translate(
    const std::vector<std::uint8_t>& rawData
) const noexcept
{
    if (!isValidPacket(rawData)) {
        return std::nullopt;
    }

    // Extract payload values
    const std::size_t payloadBase = PAYLOAD_START;
    const std::uint8_t throttleRaw = rawData[payloadBase + THROTTLE_OFFSET];
    const std::uint8_t speedLo = rawData[payloadBase + SPEED_LO_OFFSET];
    const std::uint8_t speedHi = rawData[payloadBase + SPEED_HI_OFFSET];
    const std::int8_t accelRaw = static_cast<std::int8_t>(rawData[payloadBase + ACCEL_OFFSET]);
    const std::uint8_t brakeRaw = rawData[payloadBase + BRAKE_OFFSET];

    // Convert to canonical values
    const double throttlePercent = static_cast<double>(throttleRaw);
    const double speedKmh = static_cast<double>((speedHi << 8) | speedLo);
    const double accelerationG = static_cast<double>(accelRaw) / 10.0;
    const double brakePercent = static_cast<double>(brakeRaw);

    // Get timestamp (use current time for now - would be extracted from packet if available)
    const std::uint64_t timestampUtcMs = 0; // Placeholder

    return VehicleSignal(
        timestampUtcMs,
        throttlePercent,
        speedKmh,
        accelerationG,
        brakePercent,
        std::nullopt,  // steeringAngleDeg
        std::nullopt,  // motorRpm
        std::nullopt,  // motorHvVoltage
        std::nullopt,  // motorHvCurrent
        std::nullopt,  // motorTorqueNm
        std::nullopt   // gearSelector
    );
}

std::uint8_t TeslaSignalTranslator::calculateChecksum(
    const std::vector<std::uint8_t>& packet
) const noexcept
{
    if (packet.empty()) {
        return 0;
    }

    // Sum all bytes except the last one (the checksum byte itself)
    return std::accumulate(
        packet.begin(),
        packet.end() - 1,
        static_cast<std::uint8_t>(0)
    );
}

} // namespace vehicle_sim::domain

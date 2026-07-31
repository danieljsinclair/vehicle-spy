// DiscoveredDevice.h — data contract for a discovered ESP32 device.
//
// Extracted from UDPDiscovery.h so the IDiscoveryListener interface (and any
// other consumer) can name the type WITHOUT pulling in the concrete
// UDPDiscovery class — breaking what would otherwise be a circular include
// (IDiscoveryListener.h needs DiscoveredDevice; UDPDiscovery.h derives from
// IDiscoveryListener). Pure data, no behavior dependencies.

#ifndef VEHICLE_SIM_DISCOVERY_DISCOVERED_DEVICE_H
#define VEHICLE_SIM_DISCOVERY_DISCOVERED_DEVICE_H

#include "vehicle-sim/discovery/DiscoveryPacket.h"  // DEVICE_ID_LEN

#include <array>
#include <cstdint>
#include <string>

namespace vehicle_sim::discovery {

// Information about a discovered ESP32.
struct DiscoveredDevice {
    std::array<uint8_t, DEVICE_ID_LEN> deviceId;
    std::string address;     // IP address of the ESP32
    uint16_t canPort;        // TCP port for CAN bridge
    uint16_t otaPort;        // TCP port for OTA
    uint64_t timestamp;      // Packet timestamp (Unix epoch)

    // Connection string for --connect: "tcp:<address>:<port>"
    std::string tcpConnectionString() const {
        return "tcp:" + address + ":" + std::to_string(canPort);
    }
};

}  // namespace vehicle_sim::discovery

#endif  // VEHICLE_SIM_DISCOVERY_DISCOVERED_DEVICE_H

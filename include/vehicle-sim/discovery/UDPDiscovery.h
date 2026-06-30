// UDPDiscovery.h — UDP broadcast listener for ESP32 auto-discovery.
//
// Listens on the discovery port (3335) for signed UDP broadcast packets from
// ESP32 devices. Verifies the Ed25519 signature and reports discovered devices.
//
// Usage:
//   UDPDiscovery discovery;
//   discovery.start();
//   auto devices = discovery.poll(std::chrono::seconds(5));
//   for (auto& d : devices) { ... }
//   discovery.stop();

#ifndef VEHICLE_SIM_DISCOVERY_UDP_H
#define VEHICLE_SIM_DISCOVERY_UDP_H

#include "vehicle-sim/discovery/DiscoveryPacket.h"
#include "vehicle-sim/discovery/DiscoveryVerifier.h"

#include <string>
#include <vector>
#include <chrono>
#include <array>
#include <functional>
#include <memory>
#include <atomic>

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

class UDPDiscovery {
public:
    using DeviceCallback = std::function<void(const DiscoveredDevice&)>;

    UDPDiscovery();
    ~UDPDiscovery();

    // Non-copyable
    UDPDiscovery(const UDPDiscovery&) = delete;
    UDPDiscovery& operator=(const UDPDiscovery&) = delete;

    // Start listening for UDP broadcasts.
    // Returns true on success.
    bool start();

    // Stop listening.
    void stop();

    // Check if currently listening.
    bool isListening() const;

    // Poll for discovered devices. Waits up to `timeout` for at least one
    // valid discovery packet. Returns all newly discovered devices since the
    // last poll (deduplicated by IP address).
    std::vector<DiscoveredDevice> poll(std::chrono::milliseconds timeout);

    // Set the Ed25519 public key for signature verification.
    void setPublicKey(const std::array<uint8_t, ED25519_PUBLIC_KEY_LEN>& key);

    // Set the maximum clock skew for timestamp validation (default 300s).
    void setMaxClockSkew(uint64_t seconds);

    // Set a callback that fires for each valid discovery packet received.
    void setDeviceCallback(const DeviceCallback& cb);

    // Request that poll() stop at the next iteration (called from signal handler).
    // This is a static method that sets a global flag checked by poll().
    static void requestStop() noexcept;

    // Reset the stop flag (for repeated runs).
    static void resetStop() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vehicle_sim::discovery

#endif // VEHICLE_SIM_DISCOVERY_UDP_H

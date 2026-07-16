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

#include "vehicle-sim/discovery/DiscoveredDevice.h"
#include "vehicle-sim/discovery/DiscoveryVerifier.h"
#include "vehicle-sim/discovery/IDiscoveryListener.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <string>
#include <vector>
#include <chrono>
#include <array>
#include <functional>
#include <memory>

namespace vehicle_sim::discovery {

class UDPDiscovery : public IDiscoveryListener {
public:
    using DeviceCallback = std::function<void(const DiscoveredDevice&)>;

    UDPDiscovery();
    explicit UDPDiscovery(std::shared_ptr<pipeline::StopToken> stop);
    ~UDPDiscovery();

    // Non-copyable
    UDPDiscovery(const UDPDiscovery&) = delete;
    UDPDiscovery& operator=(const UDPDiscovery&) = delete;

    // Start listening for UDP broadcasts.
    // Returns true on success.
    bool start() override;

    // Stop listening.
    void stop() override;

    // Check if currently listening.
    bool isListening() const;

    // Poll for discovered devices. Waits up to `timeout` for at least one
    // valid discovery packet. Returns all newly discovered devices since the
    // last poll (deduplicated by IP address).
    std::vector<DiscoveredDevice> poll(std::chrono::milliseconds timeout) override;

    // Set the Ed25519 public key for signature verification.
    void setPublicKey(const std::array<uint8_t, ED25519_PUBLIC_KEY_LEN>& key);

    // Set the maximum clock skew for timestamp validation (default 300s).
    void setMaxClockSkew(uint64_t seconds);

    // Set a callback that fires for each valid discovery packet received.
    void setDeviceCallback(const DeviceCallback& cb);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vehicle_sim::discovery

#endif // VEHICLE_SIM_DISCOVERY_UDP_H

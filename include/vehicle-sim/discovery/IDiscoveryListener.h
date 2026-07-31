// IDiscoveryListener.h — abstraction over a UDP device-discovery listener.
//
// TCPTransport::enterHuntingState() races reconnection-to-the-old-IP against
// a UDP discovery sweep to find the device at a new IP. Originally it
// constructed a concrete UDPDiscovery on the stack, which made the hunt
// impossible to test hermetically: a live ESP32 on the LAN broadcasts real
// signed packets on port 3335, so a host test would intermittently "win" the
// discovery race against a device that isn't its subject.
//
// This interface is the testability seam: TCPTransport depends on the
// abstraction and is injected with a FACTORY for it (see TCPTransport ctor).
// Production injects a factory that returns a real UDPDiscovery; tests inject
// a no-op listener so the discovery-win path is unreachable and the hunt
// becomes deterministic regardless of what is on the network.
//
// The operations are exactly the surface enterHuntingState() uses. They mirror
// UDPDiscovery's public start()/poll()/stop() so UDPDiscovery can implement
// this interface directly (Liskov, no adapter needed).

#ifndef VEHICLE_SIM_DISCOVERY_IDISCOVERY_LISTENER_H
#define VEHICLE_SIM_DISCOVERY_IDISCOVERY_LISTENER_H

#include "vehicle-sim/discovery/DiscoveredDevice.h"

#include <chrono>
#include <vector>

namespace vehicle_sim::discovery {

// Abstract discovery listener. See file header for rationale.
class IDiscoveryListener {
public:
    virtual ~IDiscoveryListener() = default;

    // Begin listening for discovery broadcasts. Returns true on success.
    virtual bool start() = 0;

    // Wait up to `timeout` for at least one valid discovery packet and return
    // all newly discovered devices since the last poll (deduplicated by IP).
    virtual std::vector<DiscoveredDevice> poll(std::chrono::milliseconds timeout) = 0;

    // Stop listening and release the socket.
    virtual void stop() = 0;
};

}  // namespace vehicle_sim::discovery

#endif  // VEHICLE_SIM_DISCOVERY_IDISCOVERY_LISTENER_H

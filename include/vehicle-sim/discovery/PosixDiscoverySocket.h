// PosixDiscoverySocket.h — production IDiscoverySocket.
//
// Verbatim behavior-port of UDPDiscovery::Impl's former direct POSIX socket
// calls. ZERO behavior change on the live discovery path; see IDiscoverySocket.h
// for the seam rationale and .cpp for the per-method equivalence notes.

#ifndef VEHICLE_SIM_DISCOVERY_POSIX_DISCOVERY_SOCKET_H
#define VEHICLE_SIM_DISCOVERY_POSIX_DISCOVERY_SOCKET_H

#include "vehicle-sim/discovery/IDiscoverySocket.h"

#include <cstdint>
// ssize_t is a POSIX type; this adapter is host-only (see IDiscoverySocket.h).
#ifdef __APPLE__
#include <sys/types.h>
#endif

namespace vehicle_sim::discovery {

class PosixDiscoverySocket final : public IDiscoverySocket {
public:
    PosixDiscoverySocket();
    ~PosixDiscoverySocket() override;

    PosixDiscoverySocket(const PosixDiscoverySocket&) = delete;
    PosixDiscoverySocket& operator=(const PosixDiscoverySocket&) = delete;

    bool bind(uint16_t port) override;
    bool setNonBlocking() override;
    ssize_t recvFrom(uint8_t* buf, size_t len, uint32_t* outFromAddr) override;
    int pollReadable(int timeoutMs) override;
    void close() noexcept override;

private:
    int sockfd_ = -1;
};

}  // namespace vehicle_sim::discovery

#endif  // VEHICLE_SIM_DISCOVERY_POSIX_DISCOVERY_SOCKET_H

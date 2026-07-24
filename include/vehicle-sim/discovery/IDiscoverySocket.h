// IDiscoverySocket.h — UDP socket seam for UDPDiscovery.
//
// UDPDiscovery listens for UDP broadcast discovery packets on a fixed port.
// Originally its Impl made direct POSIX calls (::socket/::bind/::recvfrom/
// ::poll/::fcntl/::close), which made the receive+parse+dedup loop impossible
// to test hermetically: driving it needed a real loopback UDP sender, which is
// flaky on a shared CI host and can be "won" by a live ESP32 broadcasting real
// packets on the LAN (#123, [[live-device-pre-empts-hunt-tests]]).
//
// This interface is the testability seam. It mirrors EXACTLY the surface
// UDPDiscovery::Impl used, so the production implementation (PosixDiscoverySocket)
// is a verbatim behavior-port — zero observable change on the live discovery
// path. Tests inject a FakeDiscoverySocket that scripts canned packets, letting
// the recvfrom→parse→dedup→callback loop run deterministically with no network.
//
// SRP: UDPDiscovery owns discovery protocol semantics (parse, dedup, callback,
// stop-token polling); IDiscoverySocket owns raw UDP socket I/O. DI: UDPDiscovery
// is constructed with a socket (default = PosixDiscoverySocket); tests inject the
// fake. This is the same pattern as pipeline::ISocket/PosixSocket (TCPTransport
// #93) and IDiscoveryListener (the outer listener seam).

#ifndef VEHICLE_SIM_DISCOVERY_IDISCOVERY_SOCKET_H
#define VEHICLE_SIM_DISCOVERY_IDISCOVERY_SOCKET_H

#include <cstddef>
#include <cstdint>
// ssize_t is a POSIX type declared by <sys/types.h>. The discovery socket seam
// is host-only (built into the host vehicle-sim lib; see IDiscoverySocket.h
// rationale), so the POSIX dependency is contained to the host build.
#ifdef __APPLE__
#include <sys/types.h>
#endif

namespace vehicle_sim::discovery {

// Raw UDP socket abstraction used by UDPDiscovery. See file header.
//
// The address is reported as a 32-bit IPv4 address in host byte order so the
// interface is portable and has no POSIX header dependency (the production
// adapter converts to/from sockaddr_in internally). A fromAddr of 0 means
// "unspecified/error" and is treated as a non-inet packet by UDPDiscovery.
class IDiscoverySocket {
public:
    virtual ~IDiscoverySocket() = default;

    // Create the UDP socket, set SO_REUSEADDR (and SO_REUSEPORT where available),
    // and bind it to the given UDP port on INADDR_ANY. Returns true on success.
    // Idempotent: a second call after success is a no-op returning true (mirrors
    // the original `if (sockfd >= 0) return true` guard).
    virtual bool bind(uint16_t port) = 0;

    // Make the socket non-blocking so the drain loop returns EAGAIN once the
    // receive queue is empty. Returns true on success. (Mirrors the original
    // fcntl(F_GETFL)/fcntl(F_SETFL, O_NONBLOCK) sequence.)
    virtual bool setNonBlocking() = 0;

    // Receive one datagram. On success returns the byte count written into buf
    // (<=len) and sets *outFromAddr to the sender's IPv4 address (host byte
    // order). On no-data-available (EAGAIN/EWOULDBLOCK) or error returns -1 and
    // leaves *outFromAddr unchanged. Mirrors POSIX recvfrom with a sockaddr_in
    // source. outFromAddr may be null.
    virtual ssize_t recvFrom(uint8_t* buf, size_t len, uint32_t* outFromAddr) = 0;

    // Wait up to timeoutMs for the socket to become readable. Returns >0 when
    // readable, 0 on timeout, <0 on error. Mirrors POSIX poll(POLLIN).
    virtual int pollReadable(int timeoutMs) = 0;

    // Close the socket and release resources (no-op if already closed). Idempotent.
    virtual void close() noexcept = 0;
};

}  // namespace vehicle_sim::discovery

#endif  // VEHICLE_SIM_DISCOVERY_IDISCOVERY_SOCKET_H

// PosixDiscoverySocket.cpp — production IDiscoverySocket: verbatim behavior-port
// of UDPDiscovery::Impl's former direct POSIX socket calls.
//
// ZERO behavior change on the live discovery path. The bind() sequence
// (socket → SO_REUSEADDR → SO_REUSEPORT → sockaddr_storage bind → non-blocking)
// and the recvFrom()/pollReadable() semantics are reproduced exactly from the
// original Impl, so live UDP discovery on port 3335 behaves identically. The
// characterization coverage (UDPDiscovery now driven through this adapter)
// proves equivalence by exercising the same code arms.

#include "vehicle-sim/discovery/PosixDiscoverySocket.h"

#include <cerrno>
#include <cstring>
#include <iostream>

// Host-only: UDPDiscovery is built into the host vehicle-sim lib (not firmware).
#ifdef __APPLE__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#endif

namespace vehicle_sim::discovery {

PosixDiscoverySocket::PosixDiscoverySocket() = default;

PosixDiscoverySocket::~PosixDiscoverySocket() {
    // close() is noexcept (it only calls ::close and guards on fd_ >= 0), so it
    // cannot throw — no try/catch needed in the dtor.
    close();
}

bool PosixDiscoverySocket::bind(uint16_t port) {
    if (sockfd_ >= 0) {
        return true;  // already bound (idempotent, mirrors original guard)
    }

    sockfd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) {
        std::cerr << "UDPDiscovery: socket() failed: " << std::strerror(errno) << "\n";
        return false;
    }

    // Allow address reuse
    int reuse = 1;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    // Bind to the requested port. Use sockaddr_storage as the storage type to
    // satisfy Sonar cpp:S3630 (avoid reinterpret_cast from sockaddr_in*). See
    // the original Impl header comment for the full rationale; the memcpy is a
    // no-op on all supported platforms.
    struct sockaddr_storage addrStorage;
    std::memset(&addrStorage, 0, sizeof(addrStorage));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    static_assert(sizeof(addr) <= sizeof(addrStorage),
                  "sockaddr_in must fit within sockaddr_storage on this platform");
    std::memcpy(&addrStorage, &addr, sizeof(addr));

    if (::bind(sockfd_, static_cast<struct sockaddr*>(static_cast<void*>(&addrStorage)),
               sizeof(addr)) < 0) {
        std::cerr << "UDPDiscovery: bind() failed on port " << port
                  << ": " << std::strerror(errno) << "\n";
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    return true;
}

bool PosixDiscoverySocket::setNonBlocking() {
    if (sockfd_ < 0) {
        return false;
    }
    // Make the socket non-blocking so the poll() drain loop (tryReceive →
    // recvfrom) returns EAGAIN immediately once the receive queue is empty
    // instead of blocking forever. (Original Impl rationale preserved.)
    if (int flags = ::fcntl(sockfd_, F_GETFL, 0); flags >= 0) {
        ::fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK);
        return true;
    }
    return false;
}

ssize_t PosixDiscoverySocket::recvFrom(uint8_t* buf, size_t len, uint32_t* outFromAddr) {
    if (sockfd_ < 0) {
        return -1;
    }

    // Halfgaar idiom (Sonar cpp:S3630): receive into sockaddr_storage, validate
    // address family, then memcpy to typed sockaddr_in. Avoids reinterpret_cast
    // from sockaddr_in* to sockaddr*.
    struct sockaddr_storage fromStorage;
    std::memset(&fromStorage, 0, sizeof(fromStorage));
    socklen_t fromLen = sizeof(fromStorage);

    ssize_t n = ::recvfrom(sockfd_, buf, len, 0,
                           static_cast<struct sockaddr*>(static_cast<void*>(&fromStorage)), &fromLen);
    if (n < 0) {
        return -1;
    }

    // IPv4-only discovery: ignore IPv6 or unknown address families.
    if (fromStorage.ss_family != AF_INET) {
        return -1;
    }

    static_assert(sizeof(struct sockaddr_in) <= sizeof(struct sockaddr_storage),
                  "sockaddr_in must fit within sockaddr_storage on this platform");
    struct sockaddr_in fromAddr;
    std::memcpy(&fromAddr, &fromStorage, sizeof(fromAddr));

    if (outFromAddr != nullptr) {
        // Report the IPv4 address in host byte order.
        *outFromAddr = ntohl(fromAddr.sin_addr.s_addr);
    }
    return n;
}

int PosixDiscoverySocket::pollReadable(int timeoutMs) {
    if (sockfd_ < 0) {
        return -1;
    }
    struct pollfd pfd;
    pfd.fd = sockfd_;
    pfd.events = POLLIN;
    return ::poll(&pfd, 1, timeoutMs);
}

void PosixDiscoverySocket::close() noexcept {
    if (sockfd_ >= 0) {
        ::close(sockfd_);
        sockfd_ = -1;
    }
}

}  // namespace vehicle_sim::discovery

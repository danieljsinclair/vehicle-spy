#include <gtest/gtest.h>

#include "vehicle-sim/discovery/PosixDiscoverySocket.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

using namespace vehicle_sim::discovery;

namespace {

// Let the OS choose a free ephemeral UDP port, returning it. Reserves then
// releases the port so PosixDiscoverySocket can bind it (with SO_REUSEADDR).
// Ephemeral ports keep the suite deterministic: no hard-coded port 3335, no
// cross-test collisions. (Same idiom as test/pipeline/PosixSocket.test.cpp.)
uint16_t ephemeralUdpPort() {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        throw std::runtime_error("socket() failed: " + std::string(std::strerror(errno)));
    }
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // OS picks
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        throw std::runtime_error("bind() failed: " + std::string(std::strerror(errno)));
    }
    sockaddr_in bound{};
    socklen_t blen = sizeof(bound);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &blen);
    const uint16_t port = ntohs(bound.sin_port);
    ::close(fd);
    return port;
}

// Send a UDP datagram to 127.0.0.1:port from a short-lived socket. Returns the
// number of bytes written (or throws on hard failure).
void sendDatagram(uint16_t port, const uint8_t* data, size_t len) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        throw std::runtime_error("socket() failed: " + std::string(std::strerror(errno)));
    }
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dest.sin_port = htons(port);
    const ssize_t n = ::sendto(fd, data, len, 0,
                               reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    ::close(fd);
    if (n < 0) {
        throw std::runtime_error("sendto() failed: " + std::string(std::strerror(errno)));
    }
}

constexpr uint32_t kLoopbackHost = 0x7F000001u;  // 127.0.0.1 in host byte order

} // namespace

// ============================================================
// PosixDiscoverySocket — production adapter characterization (loopback UDP)
//
// Drives the REAL production adapter against loopback UDP sockets with
// OS-chosen ephemeral ports. This is the same idiom as PosixSocket.test.cpp
// (TCPTransport's adapter): deterministic, no LAN, no live ESP32. It proves
// the adapter's bind/setNonBlocking/recvFrom/poll/close contract — i.e. that
// the behavior-port from the old inline POSIX in UDPDiscovery::Impl is
// equivalent, so live UDP discovery still works identically.
// ============================================================

// Contract: bind() to an ephemeral port succeeds; a second bind() is idempotent
// (no re-bind). Mirrors the original `if (sockfd >= 0) return true` guard.
TEST(PosixDiscoverySocketTest, Bind_SucceedsAndIsIdempotent) {
    PosixDiscoverySocket s;
    const uint16_t port = ephemeralUdpPort();
    ASSERT_TRUE(s.bind(port));
    EXPECT_TRUE(s.bind(port)) << "second bind is a no-op returning true";
}

// Contract: setNonBlocking() succeeds after bind; recvFrom() on an empty queue
// returns -1 (EAGAIN) instead of blocking.
TEST(PosixDiscoverySocketTest, SetNonBlocking_ThenRecvFromEmptyReturnsNegative) {
    PosixDiscoverySocket s;
    ASSERT_TRUE(s.bind(ephemeralUdpPort()));
    ASSERT_TRUE(s.setNonBlocking());

    uint8_t buf[64];
    uint32_t fromAddr = 0;
    ssize_t n = s.recvFrom(buf, sizeof(buf), &fromAddr);
    EXPECT_LT(n, 0) << "empty non-blocking queue must return <0, not block";
}

// Contract: recvFrom() receives a datagram sent to the bound port and reports
// the sender's IPv4 address (host byte order). This is the load-bearing
// equivalence with the old inline recvfrom + inet address extraction.
TEST(PosixDiscoverySocketTest, RecvFrom_ReceivesDatagramAndReportsSenderAddress) {
    PosixDiscoverySocket s;
    const uint16_t port = ephemeralUdpPort();
    ASSERT_TRUE(s.bind(port));
    ASSERT_TRUE(s.setNonBlocking());

    const uint8_t payload[] = {0x56, 0x53, 0x49, 0x4D};  // "VSIM"
    sendDatagram(port, payload, sizeof(payload));

    // pollReadable must report readable (the datagram is queued).
    ASSERT_GT(s.pollReadable(1000), 0);

    uint8_t buf[64];
    uint32_t fromAddr = 0;
    ssize_t n = s.recvFrom(buf, sizeof(buf), &fromAddr);
    ASSERT_EQ(n, static_cast<ssize_t>(sizeof(payload)));
    EXPECT_EQ(fromAddr, kLoopbackHost)
        << "sender address reported in host byte order";
    EXPECT_EQ(std::memcmp(buf, payload, sizeof(payload)), 0);
}

// Contract: pollReadable() returns 0 when no datagram arrives within the timeout
// (non-blocking, no data). Bounds the drain loop's wake-up.
TEST(PosixDiscoverySocketTest, PollReadable_NoData_ReturnsZeroOnTimeout) {
    PosixDiscoverySocket s;
    ASSERT_TRUE(s.bind(ephemeralUdpPort()));
    ASSERT_TRUE(s.setNonBlocking());

    const auto t0 = std::chrono::steady_clock::now();
    int ret = s.pollReadable(100);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    EXPECT_EQ(ret, 0);
    EXPECT_GE(elapsed.count(), 80) << "should have waited roughly the timeout";
}

// Contract: close() is idempotent and after close, recvFrom/pollReadable return
// an error (negative). Mirrors the original Impl::stop() closing the fd.
TEST(PosixDiscoverySocketTest, Close_IsIdempotentAndDisablesFurtherIO) {
    PosixDiscoverySocket s;
    ASSERT_TRUE(s.bind(ephemeralUdpPort()));

    s.close();
    s.close();  // idempotent — must not crash or double-close badly

    uint8_t buf[8];
    uint32_t fromAddr = 0;
    EXPECT_LT(s.recvFrom(buf, sizeof(buf), &fromAddr), 0);
    EXPECT_LT(s.pollReadable(10), 0);
}

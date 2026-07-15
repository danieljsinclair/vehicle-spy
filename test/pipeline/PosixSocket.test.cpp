#include <gtest/gtest.h>

#include "vehicle-sim/pipeline/PosixSocket.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <future>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace vehicle_sim::pipeline;

namespace {

// Bind a TCP listener on 127.0.0.1 with an OS-chosen ephemeral port. Returns the
// listening fd (caller closes) and writes the chosen port out. Ephemeral ports
// keep the suite deterministic: no hard-coded port, no cross-test collisions.
// Throws on failure so errors fail fast at the source instead of leaking into
// a confusing later connect() flake.
int makeListener(int& port) {
    const int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        throw std::runtime_error("socket() failed: " + std::string(strerror(errno)));
    }

    int yes = 1;
    if (::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        ::close(lfd);
        throw std::runtime_error("setsockopt(SO_REUSEADDR) failed: " + std::string(strerror(errno)));
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // OS picks a free port
    if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(lfd);
        throw std::runtime_error("bind() failed: " + std::string(strerror(errno)));
    }
    if (::listen(lfd, 1) < 0) {
        ::close(lfd);
        throw std::runtime_error("listen() failed: " + std::string(strerror(errno)));
    }

    sockaddr_in bound{};
    socklen_t blen = sizeof(bound);
    if (::getsockname(lfd, reinterpret_cast<sockaddr*>(&bound), &blen) < 0) {
        ::close(lfd);
        throw std::runtime_error("getsockname() failed: " + std::string(strerror(errno)));
    }
    port = ntohs(bound.sin_port);
    return lfd;
}

}  // namespace

// ---------------------------------------------------------------------------
// Happy path — real loopback peer in a joined thread. Each peer signals
// readiness via a future so the test never relies on wall-clock timing.
// ---------------------------------------------------------------------------

TEST(PosixSocketTest, ConnectSucceedsAgainstListeningLoopback) {
    int port = 0;
    const int lfd = makeListener(port);

    // Peer accepts the inbound connection so the kernel completes the handshake
    // and a real fd is produced at both ends.
    std::promise<int> acceptedFd;
    std::thread peer([&] {
        const int c = ::accept(lfd, nullptr, nullptr);
        acceptedFd.set_value(c);
        if (c >= 0) ::close(c);
    });

    PosixSocket s;
    const int fd = s.connect("127.0.0.1", port, nullptr);
    ASSERT_GE(fd, 0) << "PosixSocket::connect must return a real OS fd";

    // A successful connect means the peer actually accepted (not just that
    // connect() returned without error).
    auto fut = acceptedFd.get_future();
    ASSERT_EQ(fut.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
    EXPECT_GE(fut.get(), 0);

    s.close();
    ::close(lfd);
    peer.join();
}

TEST(PosixSocketTest, SendAllDeliversBytesToPeer) {
    int port = 0;
    const int lfd = makeListener(port);

    // Peer reads exactly the bytes we send and hands them back via a future.
    std::promise<std::string> received;
    std::thread peer([&] {
        const int c = ::accept(lfd, nullptr, nullptr);
        std::string buf(5, '\0');
        const ssize_t n = ::recv(c, &buf[0], buf.size(), 0);
        buf.resize(n > 0 ? static_cast<size_t>(n) : 0);
        received.set_value(buf);
        ::close(c);
    });

    PosixSocket s;
    ASSERT_GE(s.connect("127.0.0.1", port, nullptr), 0);
    ASSERT_TRUE(s.sendAll("hello")) << "sendAll must report all bytes sent";

    auto fut = received.get_future();
    ASSERT_EQ(fut.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
    EXPECT_EQ(fut.get(), "hello") << "every byte sent by sendAll must reach the peer";

    s.close();
    ::close(lfd);
    peer.join();
}

TEST(PosixSocketTest, RecvReturnsBytesPositive) {
    int port = 0;
    const int lfd = makeListener(port);

    // Peer pushes 10 bytes; test reads in 4-byte windows. The short reads
    // observed are OS behaviour — PosixSocket::recv is a one-line passthrough.
    // This is the ONLY test covering recv's POSITIVE branch (>0); Test 7 covers 0/EOF.
    std::promise<void> sent;
    std::thread peer([&] {
        const int c = ::accept(lfd, nullptr, nullptr);
        const std::string msg = "0123456789";
        ::send(c, msg.data(), msg.size(), 0);
        sent.set_value();
        ::close(c);
    });

    PosixSocket s;
    ASSERT_GE(s.connect("127.0.0.1", port, nullptr), 0);
    sent.get_future().get();  // data is in the receive buffer before we read

    std::string got;
    char buf[4];
    for (int i = 0; i < 3; ++i) {
        const ssize_t n = s.recv(buf, sizeof(buf));
        ASSERT_GT(n, 0) << "recv must return available bytes, got " << n;
        ASSERT_LE(n, static_cast<ssize_t>(sizeof(buf)))
            << "recv must honour the len cap";
        got.append(buf, static_cast<size_t>(n));
    }
    EXPECT_EQ(got, "0123456789");

    s.close();
    ::close(lfd);
    peer.join();
}

TEST(PosixSocketTest, SelectReadableSignalsWhenDataPending) {
    int port = 0;
    const int lfd = makeListener(port);
    ASSERT_GE(lfd, 0);

    // Peer sends one byte; we wait on the readiness future (not a timer) before
    // probing selectReadable, so the data is guaranteed present.
    std::promise<void> sent;
    std::thread peer([&] {
        const int c = ::accept(lfd, nullptr, nullptr);
        ::send(c, "x", 1, 0);
        sent.set_value();
        ::close(c);
    });

    PosixSocket s;
    ASSERT_GE(s.connect("127.0.0.1", port, nullptr), 0);
    sent.get_future().get();

    // Data is already pending, so this returns immediately (the readiness
    // signal). The timeout is a valid value (tv_usec must be < 1e6); its
    // magnitude is irrelevant because the socket is already readable.
    const int r = s.selectReadable(100'000);
    EXPECT_GT(r, 0) << "selectReadable must report the pending data";

    s.close();
    ::close(lfd);
    peer.join();
}

TEST(PosixSocketTest, CloseReleasesFdAndIsIdempotent) {
    int port = 0;
    const int lfd = makeListener(port);
    ASSERT_GE(lfd, 0);

    std::thread peer([&] {
        const int c = ::accept(lfd, nullptr, nullptr);
        ::close(c);
    });

    PosixSocket s;
    ASSERT_GE(s.connect("127.0.0.1", port, nullptr), 0);

    s.close();
    // After close, fd_ is invalid: a follow-up recv must fail (EBADF), proving
    // the fd was genuinely released rather than left dangling.
    char buf[1];
    EXPECT_LT(s.recv(buf, sizeof(buf)), 0);
    // A second close must be a safe no-op (idempotent) — must not crash.
    s.close();

    ::close(lfd);
    peer.join();
}

// ---------------------------------------------------------------------------
// Instant error paths (return near-instantly on loopback).
// ---------------------------------------------------------------------------

TEST(PosixSocketTest, ConnectToClosedPortReturnsFalse) {
    // Reserve an ephemeral port, then release it so nothing is listening there.
    int port = 0;
    const int probe = makeListener(port);
    ASSERT_GE(probe, 0);
    ::close(probe);

    PosixSocket s;
    const int fd = s.connect("127.0.0.1", port, nullptr);
    EXPECT_LT(fd, 0) << "connect to a closed port must fail (ECONNREFUSED)";
}

TEST(PosixSocketTest, RecvReturnsZeroOnPeerClose) {
    int port = 0;
    const int lfd = makeListener(port);
    ASSERT_GE(lfd, 0);

    // Peer accepts then immediately closes its end → EOF at our recv.
    std::promise<void> peerClosed;
    std::thread peer([&] {
        const int c = ::accept(lfd, nullptr, nullptr);
        ::close(c);
        peerClosed.set_value();
    });

    PosixSocket s;
    ASSERT_GE(s.connect("127.0.0.1", port, nullptr), 0);
    peerClosed.get_future().get();  // guarantee the peer closed first

    char buf[4];
    const ssize_t n = s.recv(buf, sizeof(buf));
    EXPECT_EQ(n, 0) << "recv must return 0 on peer close (EOF)";

    s.close();
    ::close(lfd);
    peer.join();
}

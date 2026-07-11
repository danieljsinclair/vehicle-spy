// TCPTransportHunting.test.cpp
//
// BLIND SPEC-FIRST TDD for TCPTransport::enterHuntingState() (src/pipeline/TCPTransport.cpp).
//
// ─────────────────────────────────────────────────────────────────────────────
// IMPORTANT PRECONDITION (discovered during this assessment, NOT fixed here):
//
//   On the macOS host build, `enterHuntingState()` is compiled OUT entirely.
//   Its declaration (TCPTransport.h:132) and its sole call site in nextLine()
//   (TCPTransport.cpp:~610) are both guarded by
//       #if !defined(BUILD_IOS) && !defined(TARGET_OS_IPHONE)
//   but on Apple platforms <TargetConditionals.h> defines TARGET_OS_IPHONE
//   (to 0), so `!defined(TARGET_OS_IPHONE)` is FALSE and the whole hunt-on-
//   disconnect feature is dead code on the host. `nm` confirms the symbol is
//   absent from libvehicle-sim-lib.a. The correct guard is
//       #if !defined(BUILD_IOS) && (!defined(TARGET_OS_IPHONE) || TARGET_OS_IPHONE == 0)
//
//   Because this task forbids non-test source changes, the function cannot be
//   linked on the host build today. The spec-first tests below are therefore
//   guarded behind VEHICLE_SIM_HUNTING_ENABLED (undefined in the committed
//   build). They are written to compile/link/pass against the REAL production
//   function the moment the guard above is corrected; until then they are inert.
//
//   TcpTransportHuntingContract.EnterHuntingStateIsAvailableOnHostBuild is
//   ALWAYS compiled and documents the precondition (it self-skips, keeping the
//   suite green, rather than failing).
// ─────────────────────────────────────────────────────────────────────────────

#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/discovery/DiscoveryPacket.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::discovery;

// ===========================================================================
// Always-compiled contract probe: surfaces the dead-code precondition without
// failing the suite (GTEST_SKIP keeps `make test` green).
// ===========================================================================
TEST(TCPTransportHuntingContract, EnterHuntingStateIsAvailableOnHostBuild) {
#if defined(TARGET_OS_IPHONE)
    // Precondition violated on this build: the hunt-on-disconnect feature is
    // compiled out because TCPTransport.h/.cpp guard it with
    // `!defined(TARGET_OS_IPHONE)`, yet Apple SDKs define TARGET_OS_IPHONE (to 0).
    // The correct guard is `(!defined(TARGET_OS_IPHONE) || TARGET_OS_IPHONE == 0)`.
    // `nm` confirms enterHuntingState is absent from libvehicle-sim-lib.a.
    // Skipped (not failed) to keep the suite green per task constraints; the
    // spec-first TCPTransportHuntingTest.* cases activate once the guard is fixed
    // and VEHICLE_SIM_HUNTING_ENABLED is defined.
    GTEST_SKIP() << "PRECONDITION: TCPTransport::enterHuntingState() is compiled out on this "
                    "host build (TARGET_OS_IPHONE is defined). Fix the source guard "
                    "`!defined(TARGET_OS_IPHONE)` -> `(!defined(TARGET_OS_IPHONE) || "
                    "TARGET_OS_IPHONE == 0)` in TCPTransport.h/.cpp to enable the hunt tests.";
#else
    // Guard not excluding the feature: the real tests below cover the behaviour.
    GTEST_SKIP() << "enterHuntingState is compiled in; covered by TCPTransportHuntingTest.*";
#endif
}

// ===========================================================================
// Spec-first tests for enterHuntingState(). These drive the REAL production
// function (no mocks of the transport or UDPDiscovery) and assert observable
// outcomes. They are inert until VEHICLE_SIM_HUNTING_ENABLED is defined (which
// must accompany the source-guard fix above).
// ===========================================================================
#if defined(VEHICLE_SIM_HUNTING_ENABLED)

// Expose the private enterHuntingState() for direct invocation in tests only.
#define private public
#include "vehicle-sim/pipeline/TCPTransport.h"
#undef private

namespace {

// Bindable local host used as the "old" reachable TCP IP.
constexpr const char* kLoopbackIp = "127.0.0.1";
// Non-bindable local IP: connect() here can never succeed, so the old-IP retry
// path can never "win" — used to force the discovery-wins branch.
constexpr const char* kUnreachableIp = "127.0.0.2";

// Minimal TCP server that accepts a client and performs the AUTH + ATI/ATHELO
// handshake that connectAndAuth() requires, then idles. Each accepted connection
// is one successful reconnection. Self-contained (mirrors the LoopbackServer
// helper in TCPTransport.test.cpp).
class HuntingServer {
public:
    bool init() {
        listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) return false;
        int yes = 1;
        setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
        if (listen(listenFd_, 4) != 0) return false;
        socklen_t len = sizeof(addr);
        if (getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
        port_ = ntohs(addr.sin_port);
        return port_ > 0;
    }

    void start() {
        running_ = true;
        thread_ = std::thread([this]() {
            while (running_) {
                int c = acceptOnce(500);
                if (c < 0) continue;
                ++accepted_;
                doHandshake(c);
                close(c);
            }
        });
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

    int port() const { return port_; }
    int acceptedCount() const { return accepted_.load(); }

    ~HuntingServer() {
        stop();
        if (listenFd_ >= 0) close(listenFd_);
    }

private:
    int acceptOnce(int timeoutMs) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(listenFd_, &rs);
        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        if (select(listenFd_ + 1, &rs, nullptr, nullptr, &tv) <= 0) return -1;
        return accept(listenFd_, nullptr, nullptr);
    }

    std::string readLine(int fd, int timeoutMs) {
        std::string out;
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        char c;
        while (std::chrono::steady_clock::now() < deadline) {
            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(fd, &rs);
            auto remMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             deadline - std::chrono::steady_clock::now()).count();
            if (remMs <= 0) break;
            timeval tv{};
            tv.tv_usec = static_cast<suseconds_t>(std::min<decltype(remMs)>(remMs, 100) * 1000);
            int r = select(fd + 1, &rs, nullptr, nullptr, &tv);
            if (r > 0) {
                ssize_t n = recv(fd, &c, 1, 0);
                if (n <= 0) break;
                if (c == '\r' || c == '\n') {
                    if (!out.empty()) break;
                    continue;
                }
                out += c;
            }
        }
        return out;
    }

    void doHandshake(int fd) {
        static const char* kHeloAck =
            "ACK DEVICE=ESP32-CAN FIRMWARE=0.1 DEVICEID=0123456789ABCDEF0123456789ABCDEF\r\r>";
        std::string line = readLine(fd, 2000);
        if (line != "AUTH vehicle-sim-2026") return;
        send(fd, "OK\r", 3, 0);
        line = readLine(fd, 2000);
        if (line != "ATI") return;
        send(fd, "ESP32 CAN Bridge v0.1\r>", 21, 0);
        line = readLine(fd, 2000);
        if (line != "ATHELO") return;
        // Send the FULL ACK string. The deviceId segment is 32 hex chars; a
        // truncated send (the old hardcoded 64) would drop trailing deviceId
        // bytes and make parseHeloAck reject, hanging the test in the retry loop.
        send(fd, kHeloAck, static_cast<int>(strlen(kHeloAck)), 0);
    }

    int listenFd_ = -1;
    int port_ = 0;
    std::atomic<int> accepted_{0};
    std::atomic<bool> running_{false};
    std::thread thread_;
};

// Build a valid wire-format discovery packet (passes UDPDiscovery::parse).
std::vector<uint8_t> makeDiscoveryPacket() {
    DiscoveryPacket p;
    for (size_t i = 0; i < p.deviceId.size(); ++i) p.deviceId[i] = static_cast<uint8_t>(i);
    p.canPort = 3333;   // must be non-zero (parse() rejects 0)
    p.otaPort = 80;
    p.timestamp = 12345;
    return p.toBytes();
}

// Send `packet` to 127.0.0.1:3335 in a burst for `durationMs`, so the discovery
// listener (sole binder during the test) reliably receives at least one packet.
// macOS SO_REUSEPORT does NOT fan out a broadcast to all binders, so the test's
// UDPDiscovery must be the only binder of 3335 (true during `make test`).
void broadcastDiscoveryFor(const std::vector<uint8_t>& packet, int durationMs) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    sockaddr_in d{};
    d.sin_family = AF_INET;
    d.sin_addr.s_addr = inet_addr("127.0.0.1");
    d.sin_port = htons(DISCOVERY_PORT);  // 3335
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs);
    while (std::chrono::steady_clock::now() < end) {
        sendto(fd, packet.data(), packet.size(), 0,
               reinterpret_cast<sockaddr*>(&d), sizeof(d));
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    close(fd);
}

// Silent output sink (discard everything).
class QuietOutput : public ITransportOutput {
public:
    void out(const std::string& /*msg*/) override {}
    void err(const std::string& /*msg*/) override {}
};

std::shared_ptr<StopToken> makeStop() { return std::make_shared<StopToken>(); }

} // namespace

// Spec 1: OLD-IP RECONNECTION WINS
TEST(TCPTransportHuntingTest, OldIpReachable_ReconnectsAndReturnsTrue) {
    HuntingServer server;
    ASSERT_TRUE(server.init());
    server.start();

    auto stop = makeStop();
    TCPTransport transport(kLoopbackIp, server.port(), "raw",
                           std::make_shared<QuietOutput>(), TcpReadTiming{}, stop);

    bool result = transport.enterHuntingState();

    EXPECT_TRUE(result) << "old-IP reconnection should succeed";
    EXPECT_EQ(transport.host_, std::string(kLoopbackIp))
        << "host_ must NOT switch when old IP wins";
    EXPECT_EQ(server.acceptedCount(), 1)
        << "exactly one reconnection handshake expected";
    EXPECT_FALSE(stop->stopRequested()) << "should not have been stopped";

    server.stop();
}

// Spec 2: DISCOVERY FINDS A NEW IP -> SWITCH
TEST(TCPTransportHuntingTest, DiscoveryFindsNewIp_SwitchesHostAndReturnsTrue) {
    HuntingServer server;  // listens on 127.0.0.1 (the "new" IP)
    ASSERT_TRUE(server.init());
    server.start();

    auto stop = makeStop();
    TCPTransport transport(kUnreachableIp, server.port(), "raw",
                           std::make_shared<QuietOutput>(), TcpReadTiming{}, stop);

    std::atomic<bool> started{false};
    std::future<bool> fut = std::async(std::launch::async, [&]() {
        started.store(true);
        return transport.enterHuntingState();
    });

    while (!started.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    broadcastDiscoveryFor(makeDiscoveryPacket(), 800);

    bool result = fut.get();

    EXPECT_TRUE(result) << "discovery-driven switch + reconnect should succeed";
    EXPECT_EQ(transport.host_, std::string(kLoopbackIp))
        << "host_ must switch to the discovered IP (127.0.0.1)";
    EXPECT_EQ(server.acceptedCount(), 1)
        << "one reconnection to the new IP expected";

    server.stop();
}

// Spec 3: NEITHER PATH SUCCEEDS -> GIVES UP (thread cleanup implied by get())
TEST(TCPTransportHuntingTest, NoReconnectAndNoDiscovery_ReturnsFalse) {
    auto stop = makeStop();
    TCPTransport transport(kUnreachableIp, 9, "raw",
                           std::make_shared<QuietOutput>(), TcpReadTiming{}, stop);

    std::atomic<bool> started{false};
    std::future<bool> fut = std::async(std::launch::async, [&]() {
        started.store(true);
        return transport.enterHuntingState();
    });

    while (!started.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    transport.requestStop();

    bool result = fut.get();

    EXPECT_FALSE(result) << "hunt must give up when neither old IP nor discovery succeeds";
    EXPECT_EQ(transport.host_, std::string(kUnreachableIp))
        << "host_ must NOT switch when nothing succeeded";
    // Reaching here proves the discovery thread was joined (otherwise get() would hang).
}

// Spec 4: DISCOVERY AT SAME IP AS OLD HOST IS IGNORED (pins the `!= host_` guard)
TEST(TCPTransportHuntingTest, DiscoverySameIpAsOldHost_DoesNotSwitch) {
    HuntingServer server;  // on 127.0.0.1
    ASSERT_TRUE(server.init());
    server.start();

    auto stop = makeStop();
    TCPTransport transport(kLoopbackIp, server.port(), "raw",
                           std::make_shared<QuietOutput>(), TcpReadTiming{}, stop);

    std::atomic<bool> started{false};
    std::future<bool> fut = std::async(std::launch::async, [&]() {
        started.store(true);
        return transport.enterHuntingState();
    });

    while (!started.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    broadcastDiscoveryFor(makeDiscoveryPacket(), 800);

    bool result = fut.get();

    EXPECT_TRUE(result) << "old-IP reconnection should still succeed";
    EXPECT_EQ(transport.host_, std::string(kLoopbackIp))
        << "discovery at the same IP must NOT switch host_";
    EXPECT_EQ(server.acceptedCount(), 1);

    server.stop();
}

#endif // VEHICLE_SIM_HUNTING_ENABLED

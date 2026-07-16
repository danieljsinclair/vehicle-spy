// TCPTransportHuntingGap.test.cpp
//
// BLIND SPEC-FIRST TDD — characterization tests pinning the refactor-risk
// behavior of TCPTransport::enterHuntingState() + nextLine() that the existing
// hunting suites do NOT yet cover.
//
// HERMETIC: network I/O is scripted through FakeSocket (no real socket /
// loopback server), time is instant via a FakeClock (backoff + handshake pacing
// route through IClock::sleepFor), and every discovery-dependent test injects a
// custom IDiscoveryListener via the DiscoveryListenerFactory ctor seam (no real
// UDP). The original assertions are preserved exactly.

#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/discovery/DiscoveredDevice.h"
#include "vehicle-sim/discovery/IDiscoveryListener.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/pipeline/FakeSocket.h"
#include "vehicle-sim/util/IClock.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace vehicle_sim::pipeline;
namespace util = vehicle_sim::util;
using namespace vehicle_sim::discovery;

#define private public
#include "vehicle-sim/pipeline/TCPTransport.h"
#undef private

#if defined(VEHICLE_SIM_HUNTING_ENABLED)

namespace {

constexpr const char* kLoopbackIp = "127.0.0.1";
constexpr const char* kUnreachableIp = "127.0.0.2";
constexpr const char* kUnreachableIp2 = "127.0.0.3";
constexpr int kDiscardPort = 9;

std::shared_ptr<StopToken> makeStop() { return std::make_shared<StopToken>(); }

std::shared_ptr<ITransportOutput> silentOutput() {
    return std::make_shared<SilentOutput>();
}

// Capturing output sink: stores every out()/err() string for later assertion.
class CapturingOutput : public ITransportOutput {
public:
    void out(const std::string& msg) override {
        std::lock_guard<std::mutex> lk(mu_);
        outLines_.push_back(msg);
    }
    void err(const std::string& msg) override {
        std::lock_guard<std::mutex> lk(mu_);
        errLines_.push_back(msg);
    }
    std::vector<std::string> errLines() const {
        std::lock_guard<std::mutex> lk(mu_);
        return errLines_;
    }
    std::string errBlob() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::string s;
        for (const auto& l : errLines_) { s += l; s += "\n"; }
        return s;
    }
    std::string allBlob() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::string s;
        for (const auto& l : outLines_) { s += l; s += "\n"; }
        for (const auto& l : errLines_) { s += l; s += "\n"; }
        return s;
    }
private:
    mutable std::mutex mu_;
    std::vector<std::string> outLines_;
    std::vector<std::string> errLines_;
};

// Configurable discovery listener: always reports ONE device with a given
// 16-byte deviceId + address. Hermetic — no real UDP.
class FixedDiscoveryListener : public IDiscoveryListener {
public:
    FixedDiscoveryListener(std::array<uint8_t, 16> deviceId, std::string address)
        : deviceId_(deviceId), address_(std::move(address)) {}
    bool start() override { return true; }
    std::vector<DiscoveredDevice> poll(std::chrono::milliseconds /*timeout*/) override {
        DiscoveredDevice d{};
        d.deviceId = deviceId_;
        d.address = address_;
        d.canPort = 3333;
        d.otaPort = 80;
        d.timestamp = 12345;
        return {d};
    }
    void stop() override {}
private:
    std::array<uint8_t, 16> deviceId_;
    std::string address_;
};

DiscoveryListenerFactory fixedDiscoveryFactory(std::array<uint8_t, 16> deviceId,
                                              std::string address) {
    return [id = deviceId, addr = std::move(address)]() {
        return std::unique_ptr<IDiscoveryListener>(
            std::make_unique<FixedDiscoveryListener>(id, addr));
    };
}

// No-op discovery listener: never reports a device.
class NoOpDiscoveryListener : public IDiscoveryListener {
public:
    bool start() override { return true; }
    std::vector<DiscoveredDevice> poll(std::chrono::milliseconds /*timeout*/) override {
        return {};
    }
    void stop() override {}
};

DiscoveryListenerFactory noOpDiscoveryFactory() {
    return []() { return std::make_unique<NoOpDiscoveryListener>(); };
}

// Build a transport wired to a scripted FakeSocket (non-owning) + instant
// FakeClock + the given discovery factory. `huntStartedProm` (optional) is
// shared with the test so it can await hunt-start on the onHuntStarted signal
// (zero sleep); onHuntStarted is added to `hunt` when provided.
std::unique_ptr<TCPTransport> makeTransport(
    test::FakeSocket& sock, const std::string& host, int port,
    std::shared_ptr<StopToken> stop, std::shared_ptr<ITransportOutput> out,
    HuntResilienceConfig hunt = HuntResilienceConfig{},
    std::shared_ptr<std::promise<void>> huntStartedProm = nullptr) {
    if (huntStartedProm) {
        hunt.onHuntStarted = [huntStartedProm]() { huntStartedProm->set_value(); };
    }
    auto clock = std::make_shared<util::FakeClock>();
    return std::make_unique<TCPTransport>(
        TransportEndpoint{host, port, "raw"},
        std::move(out), TcpReadTiming{}, std::move(stop),
        std::move(hunt), std::move(clock),
        std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
}

std::array<uint8_t, 16> heloDeviceIdBytes() {
    return {{0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
             0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF}};
}
std::array<uint8_t, 16> prefixMatchDeviceIdBytes() {
    return {{0x01,0x23,0x45,0x67,0xFF,0xFF,0xFF,0xFF,
             0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}};
}
std::array<uint8_t, 16> nonMatchDeviceIdBytes() {
    std::array<uint8_t, 16> a{};
    a.fill(0xFF);
    return a;
}

// Await the hunt going live on the onHuntStarted signal (zero polling/sleep):
// the production enterHuntingState() fires the hook once at its retry-loop top.
void awaitHuntStarted(std::future<void>& huntStartedFut) {
    huntStartedFut.wait();
}

long elapsedMs(std::chrono::steady_clock::time_point from,
               std::chrono::steady_clock::time_point to) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
}

// Open the transport (consuming the caller-enqueued open handshake script).
// The caller must enqueue the open script FIRST so the queue order is
// deterministic across the whole hunt (open, then per-hunt reconnect scripts).
bool openAgainstSocket(TCPTransport& transport, test::FakeSocket& /*sock*/) {
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = transport.open(); });
    th.join();
    return opened.load();
}

// Open the transport, then reassign host_ to an unreachable IP so the
// subsequent hunt's old-IP-reconnect path FAILS (forcing discovery to decide).
void openForDeviceIdThenReassignHost(TCPTransport& transport, test::FakeSocket& sock,
                                     const char* newHost) {
    ASSERT_TRUE(openAgainstSocket(transport, sock))
        << "open() must succeed to set deviceIdHex_ via HELO";
    transport.requestStop();
    transport.nextLine();
    transport.resetStop();
    transport.host_ = newHost;
}

} // namespace

// ===========================================================================
// G1: deviceIdHex EMPTY → accepts FIRST device whose IP ≠ host_ (match-all).
// ===========================================================================
TEST(TCPTransportHuntingGapTest, G1_EmptyDeviceIdHex_AcceptsFirstDeviceAtNewIp) {
    test::FakeSocket sock;
    sock.enqueue(kUnreachableIp, test::failConnect());        // old IP (unreachable) fails
    sock.enqueue(kLoopbackIp, test::handshakeConnect());   // discovered new IP (127.0.0.1) succeeds

    auto stop = makeStop();
    auto transport = makeTransport(sock, kUnreachableIp, 3333, stop, silentOutput(),
                                   HuntResilienceConfig{fixedDiscoveryFactory(heloDeviceIdBytes(), kLoopbackIp), {}});

    bool result = transport->enterHuntingState();

    EXPECT_TRUE(result) << "match-all (empty deviceIdHex) should accept the first device";
    EXPECT_EQ(transport->host_, std::string(kLoopbackIp))
        << "host_ must switch to the discovered new IP";
    EXPECT_EQ(sock.connectCount(), 1)
        << "one reconnect to the discovered IP expected";
}

// ===========================================================================
// G2: deviceIdHex SET → exact match wins; non-match ignored.
// ===========================================================================
TEST(TCPTransportHuntingGapTest, G2_DeviceIdSet_ExactMatchWins_NonMatchIgnored) {
    // --- (a) non-match: discovery device has all-0xFF id → ignored ---
    {
        test::FakeSocket sock;
        sock.enqueue(kLoopbackIp, test::handshakeConnect());  // open() sets deviceIdHex_
        sock.enqueue(kUnreachableIp, test::failConnect());        // hunt: old IP unreachable, fails

        auto stop = makeStop();
        auto huntStartedProm = std::make_shared<std::promise<void>>();
        std::future<void> huntStartedFut = huntStartedProm->get_future();
        auto transport = makeTransport(sock, kLoopbackIp, 3333, stop, silentOutput(),
                                       HuntResilienceConfig{fixedDiscoveryFactory(nonMatchDeviceIdBytes(), kLoopbackIp), {}},
                                       huntStartedProm);
        openForDeviceIdThenReassignHost(*transport, sock, kUnreachableIp);

        std::future<bool> fut = std::async(std::launch::async, [&]() {
            return transport->enterHuntingState();
        });
        awaitHuntStarted(huntStartedFut);
        transport->requestStop();
        ASSERT_EQ(fut.wait_for(std::chrono::milliseconds(3000)), std::future_status::ready);
        bool result = fut.get();
        EXPECT_FALSE(result)
            << "non-matching deviceId must be ignored → no discovery win";
        EXPECT_EQ(transport->host_, std::string(kUnreachableIp))
            << "host_ must NOT switch when the discovered device doesn't match";
    }

    // --- (b) exact match: discovery device has the full HELO id → switch ---
    {
        test::FakeSocket sock;
        sock.enqueue(kLoopbackIp, test::handshakeConnect());  // open() sets deviceIdHex_
        sock.enqueue(kUnreachableIp, test::failConnect());        // old IP unreachable during hunt
        sock.enqueue(kLoopbackIp, test::handshakeConnect());   // discovered-IP (127.0.0.1) connect

        auto stop = makeStop();
        auto transport = makeTransport(sock, kLoopbackIp, 3333, stop, silentOutput(),
                                       HuntResilienceConfig{fixedDiscoveryFactory(heloDeviceIdBytes(), kLoopbackIp), {}});
        openForDeviceIdThenReassignHost(*transport, sock, kUnreachableIp);

        bool result = transport->enterHuntingState();
        EXPECT_TRUE(result)
            << "exact-match deviceId should win discovery → reconnect + true";
        EXPECT_EQ(transport->host_, std::string(kLoopbackIp))
            << "host_ must switch to the exact-match discovered IP";
    }
}

// ===========================================================================
// G3 (HIGHEST VALUE): deviceIdHex SET → 8-char PREFIX match wins.
// ===========================================================================
TEST(TCPTransportHuntingGapTest, G3_DeviceIdSet_EightCharPrefixMatchWins) {
    test::FakeSocket sock;
    sock.enqueue(kLoopbackIp, test::handshakeConnect());  // open() sets deviceIdHex_
    sock.enqueue(kUnreachableIp, test::failConnect());        // old IP unreachable during hunt
    sock.enqueue(kLoopbackIp, test::handshakeConnect());   // discovered-IP (127.0.0.1) connect

    auto stop = makeStop();
    auto transport = makeTransport(sock, kLoopbackIp, 3333, stop, silentOutput(),
                                   HuntResilienceConfig{fixedDiscoveryFactory(prefixMatchDeviceIdBytes(), kLoopbackIp), {}});
    openForDeviceIdThenReassignHost(*transport, sock, kUnreachableIp);

    bool result = transport->enterHuntingState();

    EXPECT_TRUE(result)
        << "8-char-prefix match should win discovery → reconnect + true";
    EXPECT_EQ(transport->host_, std::string(kLoopbackIp))
        << "host_ must switch to the prefix-match discovered IP";
}

// ===========================================================================
// G5: retryCount_ reset to 0 on successful reconnect.
// ===========================================================================
TEST(TCPTransportHuntingGapTest, G5_RetryCountResetOnSuccess_SecondHuntSucceeds) {
    test::FakeSocket sock;
    sock.enqueue(kLoopbackIp, test::handshakeConnect());  // first hunt: old-IP reconnect wins
    sock.enqueue(kLoopbackIp, test::handshakeConnect());  // second hunt: old-IP reconnect wins again

    auto stop = makeStop();
    auto transport = makeTransport(sock, kLoopbackIp, 3333, stop, silentOutput(),
                                   HuntResilienceConfig{noOpDiscoveryFactory(), {}});

    auto t0 = std::chrono::steady_clock::now();
    bool first = transport->enterHuntingState();
    auto t1 = std::chrono::steady_clock::now();
    ASSERT_TRUE(first) << "first hunt (old-IP reconnect) should succeed";
    long firstMs = elapsedMs(t0, t1);

    transport->requestStop();
    transport->nextLine();
    transport->resetStop();

    auto t2 = std::chrono::steady_clock::now();
    bool second = transport->enterHuntingState();
    auto t3 = std::chrono::steady_clock::now();
    EXPECT_TRUE(second) << "second hunt should also succeed (retryCount_ reset)";
    long secondMs = elapsedMs(t2, t3);
    EXPECT_LE(secondMs, firstMs + TCPTransport::BASE_RETRY_DELAY_MS)
        << "second hunt took " << secondMs << "ms vs first " << firstMs
        << "ms; a carried-over retryCount_ would elevate the starting backoff";
    EXPECT_GE(sock.connectCount(), 2)
        << "socket should have re-connected for the second hunt";
}

// ===========================================================================
// G6 + G7 (combined): backoff schedule + outcome message formats.
// ===========================================================================
TEST(TCPTransportHuntingGapTest, G6G7_BackoffScheduleAndOutcomeMessages) {
    // --- (a) backoff schedule: fast-fail host + no-op discovery → give-up ---
    {
        auto cap = std::make_shared<CapturingOutput>();
        test::FakeSocket sock;
        sock.enqueue(kLoopbackIp, test::failConnect());  // 127.0.0.1:9 connect fails instantly

        auto stop = makeStop();
        auto huntStartedProm = std::make_shared<std::promise<void>>();
        std::future<void> huntStartedFut = huntStartedProm->get_future();
        auto transport = makeTransport(sock, kLoopbackIp, kDiscardPort, stop, cap,
                                       HuntResilienceConfig{noOpDiscoveryFactory(), {}},
                                       huntStartedProm);

        std::future<bool> fut = std::async(std::launch::async, [&]() {
            return transport->enterHuntingState();
        });
        awaitHuntStarted(huntStartedFut);
        // With FakeClock the backoff is instant, so the hunt self-exhausts all
        // 60 retries in ms and returns false via finalizeHunt.
        ASSERT_EQ(fut.wait_for(std::chrono::milliseconds(5000)), std::future_status::ready)
            << "hunt should self-exhaust under the instant backoff";
        transport->requestStop();
        fut.get();

        std::string msgs = cap->allBlob();
        EXPECT_NE(msgs.find("1000ms"), std::string::npos)
            << "backoff attempt 1 should report a 1000ms delay; msg blob:\n" << msgs;
        bool hasLargerDelay = (msgs.find("2000ms") != std::string::npos) ||
                              (msgs.find("4000ms") != std::string::npos);
        EXPECT_TRUE(hasLargerDelay)
            << "backoff should grow exponentially (a later attempt should "
            << "report a larger delay than 1000ms); msg blob:\n" << msgs;
        EXPECT_NE(msgs.find("[tcp] hunting: retrying old IP"), std::string::npos)
            << "retrying-old-IP message format must match; msg blob:\n" << msgs;
        EXPECT_NE(msgs.find("(attempt "), std::string::npos)
            << "attempt-number substring must be present; msg blob:\n" << msgs;
        EXPECT_NE(msgs.find("/60"), std::string::npos)
            << "attempt N/60 denominator must be present; msg blob:\n" << msgs;
        EXPECT_NE(msgs.find(TCPTransport::kClientTag), std::string::npos)
            << "client tag must be present on retry messages; msg blob:\n" << msgs;
        EXPECT_NE(msgs.find("[tcp] hunting: neither old IP nor discovery succeeded"),
                  std::string::npos)
            << "give-up outcome message must be present; msg blob:\n" << msgs;
    }

    // --- (b) old-IP win outcome message ---
    {
        auto capOld = std::make_shared<CapturingOutput>();
        test::FakeSocket sock;
        sock.enqueue(kLoopbackIp, test::handshakeConnect());  // old-IP (127.0.0.1) wins on attempt 1

        auto stop = makeStop();
        auto transport = makeTransport(sock, kLoopbackIp, 3333, stop, capOld,
                                       HuntResilienceConfig{noOpDiscoveryFactory(), {}});
        bool result = transport->enterHuntingState();
        EXPECT_TRUE(result);
        std::string msgs = capOld->allBlob();
        EXPECT_NE(msgs.find("[tcp] hunting: reconnected to old IP"), std::string::npos)
            << "old-IP-win outcome message must be present; msg blob:\n" << msgs;
    }

    // --- (c) discovery-win outcome message + 8-char-prefix ESP32 tag ---
    {
        auto capDisc = std::make_shared<CapturingOutput>();
        test::FakeSocket sock;
        sock.enqueue(kLoopbackIp, test::handshakeConnect());  // open() sets deviceIdHex_
        sock.enqueue(kUnreachableIp, test::failConnect());        // old IP unreachable during hunt
        sock.enqueue(kLoopbackIp, test::handshakeConnect());   // discovered-IP (127.0.0.1) connect

        auto stop = makeStop();
        auto transport = makeTransport(sock, kLoopbackIp, 3333, stop, capDisc,
                                       HuntResilienceConfig{fixedDiscoveryFactory(prefixMatchDeviceIdBytes(), kLoopbackIp), {}});
        openForDeviceIdThenReassignHost(*transport, sock, kUnreachableIp);

        bool result = transport->enterHuntingState();
        EXPECT_TRUE(result);
        std::string msgs = capDisc->allBlob();
        EXPECT_NE(msgs.find("[tcp] hunting: switching to discovered IP"), std::string::npos)
            << "discovery-win 'switching' message must be present; msg blob:\n" << msgs;
        EXPECT_NE(msgs.find("[tcp] hunting: connected to new IP"), std::string::npos)
            << "discovery-win 'connected to new IP' message must be present; msg blob:\n" << msgs;
        EXPECT_NE(msgs.find("[ESP32:01234567]"), std::string::npos)
            << "discovery-win ESP32 tag (8-char prefix) must be present; msg blob:\n" << msgs;
    }

    // --- (d) discovery-win-but-new-IP-connect-fails outcome message ---
    {
        auto capFail = std::make_shared<CapturingOutput>();
        test::FakeSocket sock;
        sock.enqueue(kLoopbackIp, test::handshakeConnect());  // open() sets deviceIdHex_
        sock.enqueue(kUnreachableIp, test::failConnect());        // old IP unreachable during hunt
        sock.enqueue(kUnreachableIp2, test::failConnect());        // discovered new IP (127.0.0.3) connect fails

        auto stop = makeStop();
        auto huntStartedProm = std::make_shared<std::promise<void>>();
        std::future<void> huntStartedFut = huntStartedProm->get_future();
        auto transport = makeTransport(sock, kLoopbackIp, 3333, stop, capFail,
                                       HuntResilienceConfig{fixedDiscoveryFactory(prefixMatchDeviceIdBytes(), kUnreachableIp2), {}},
                                       huntStartedProm);
        openForDeviceIdThenReassignHost(*transport, sock, kUnreachableIp);

        std::future<bool> fut = std::async(std::launch::async, [&]() {
            return transport->enterHuntingState();
        });
        awaitHuntStarted(huntStartedFut);
        ASSERT_EQ(fut.wait_for(std::chrono::milliseconds(8000)), std::future_status::ready);
        bool result = fut.get();
        EXPECT_FALSE(result)
            << "discovery win + new-IP connect failure must return false";

        std::string msgs = capFail->allBlob();
        EXPECT_NE(msgs.find("[tcp] hunting: failed to connect to new IP"), std::string::npos)
            << "new-IP-connect-failed outcome message must be present; msg blob:\n" << msgs;
        EXPECT_NE(msgs.find("giving up"), std::string::npos)
            << "give-up substring must be present on new-IP failure; msg blob:\n" << msgs;
    }
}

// ===========================================================================
// G8: stop during backoff sleep → interrupts within ~one 100ms checkInterval.
// ===========================================================================
TEST(TCPTransportHuntingGapTest, G8_StopDuringBackoffInterruptsWithinOneSlice) {
    test::FakeSocket sock;
    sock.enqueue(kUnreachableIp, test::failConnect());  // old IP unreachable, connect always fails

    auto stop = makeStop();
    auto huntStartedProm = std::make_shared<std::promise<void>>();
    std::future<void> huntStartedFut = huntStartedProm->get_future();
    auto transport = makeTransport(sock, kUnreachableIp, kDiscardPort, stop, silentOutput(),
                                   HuntResilienceConfig{noOpDiscoveryFactory(), {}},
                                   huntStartedProm);

    std::future<bool> fut = std::async(std::launch::async, [&]() {
        return transport->enterHuntingState();
    });
    awaitHuntStarted(huntStartedFut);
    auto stopAt = std::chrono::steady_clock::now();
    transport->requestStop();

    ASSERT_EQ(fut.wait_for(std::chrono::milliseconds(2000)), std::future_status::ready)
        << "hunt must return promptly after requestStop (backoff slicing)";
    auto returnedAt = std::chrono::steady_clock::now();
    long postStop = elapsedMs(stopAt, returnedAt);
    bool result = fut.get();

    EXPECT_FALSE(result) << "cancelled hunt reports no connection";
    EXPECT_LE(postStop, 200)
        << "stop-during-backoff took " << postStop << "ms to interrupt; the "
        << "100ms-sliced backoff should catch a stop within ~one slice (a "
        << "single-sleep_for(1000) regression would fail this)";
}

// ===========================================================================
// G11: discovery finds new IP but connectAndAuth to new IP FAILS → returns
//   false AND host_ HAS switched to the discovered (failed) IP.
// ===========================================================================
TEST(TCPTransportHuntingGapTest, G11_DiscoveryWinButNewIpConnectFails_HostSwitched) {
    test::FakeSocket sock;
    sock.enqueue(kLoopbackIp, test::handshakeConnect());  // open() sets deviceIdHex_
    sock.enqueue(kUnreachableIp, test::failConnect());        // old IP unreachable during hunt
    sock.enqueue(kUnreachableIp2, test::failConnect());        // discovered new IP (127.0.0.3) connect fails

    auto stop = makeStop();
    auto huntStartedProm = std::make_shared<std::promise<void>>();
    std::future<void> huntStartedFut = huntStartedProm->get_future();
    auto transport = makeTransport(sock, kLoopbackIp, 3333, stop, silentOutput(),
                                   HuntResilienceConfig{fixedDiscoveryFactory(prefixMatchDeviceIdBytes(), kUnreachableIp2), {}},
                                   huntStartedProm);
    openForDeviceIdThenReassignHost(*transport, sock, kUnreachableIp);

    std::future<bool> fut = std::async(std::launch::async, [&]() {
        return transport->enterHuntingState();
    });
    awaitHuntStarted(huntStartedFut);
    ASSERT_EQ(fut.wait_for(std::chrono::milliseconds(8000)), std::future_status::ready)
        << "hunt should return after the new-IP connect fails";
    bool result = fut.get();

    EXPECT_FALSE(result)
        << "discovery win but new-IP connect failure must return false";
    EXPECT_EQ(transport->host_, std::string(kUnreachableIp2))
        << "host_ must have switched to the discovered (failed) IP BEFORE the "
        << "connect attempt — NOT the original old IP";
}

// ===========================================================================
// N1 (HIGHEST VALUE): recv returns 0 (peer close) + NO stop + deviceIdHex SET →
//   enters hunting, reconnects, continues reading.
// ===========================================================================
TEST(TCPTransportHuntingGapTest, N1_PeerCloseTriggersHuntReconnectAndContinuesReading) {
    test::FakeSocket sock;
    std::deque<std::string> first;
    for (auto& c : test::heloHandshakeChunks()) first.push_back(c);
    first.push_back("FIRST_LINE\r");
    sock.enqueue(kLoopbackIp, test::FakeConnectScript{true, std::move(first)});
    // On the SECOND connect (the hunt's reconnect), deliver SECOND_LINE.
    std::deque<std::string> second;
    for (auto& c : test::heloHandshakeChunks()) second.push_back(c);
    second.push_back("SECOND_LINE\r");
    sock.enqueue(kLoopbackIp, test::FakeConnectScript{true, std::move(second)});

    auto stop = makeStop();
    auto transport = makeTransport(sock, kLoopbackIp, 3333, stop, silentOutput(),
                                   HuntResilienceConfig{noOpDiscoveryFactory(), {}});
    ASSERT_TRUE(openAgainstSocket(*transport, sock))
        << "open() must succeed (sets deviceIdHex_ + connects)";

    auto firstLine = transport->nextLine();
    ASSERT_TRUE(firstLine.has_value()) << "first line should be readable post-open";
    EXPECT_EQ(*firstLine, "FIRST_LINE");

    // The scripted connection delivers no more bytes after FIRST_LINE, so the
    // next recv returns 0 (peer close) → nextLine enters hunting, reconnects,
    // and (per spec) continues reading → returns the SECOND line.
    std::optional<std::string> secondLine;
    std::future<void> fut = std::async(std::launch::async, [&]() {
        secondLine = transport->nextLine();
    });

    auto status = fut.wait_for(std::chrono::milliseconds(8000));
    ASSERT_EQ(status, std::future_status::ready)
        << "N1 SPEC/CODE MISMATCH: nextLine did NOT return after reconnect+read. "
        << "Socket connectCount=" << sock.connectCount()
        << " (>1 means the hunt DID reconnect).";

    if (secondLine.has_value()) {
        EXPECT_EQ(*secondLine, "SECOND_LINE")
            << "nextLine must return the SECOND line, proving reconnect+continue";
    } else {
        FAIL() << "nextLine returned nullopt after reconnect (socket connectCount="
               << sock.connectCount() << ") but spec says it should return "
               << "the SECOND line (reconnect+continue in one call).";
    }

    transport->requestStop();
    transport->nextLine();
    stop->reset();
}

// ===========================================================================
// N5: exhausted_ is sticky. After a peer-close nullopt, a subsequent nextLine()
//   returns nullopt IMMEDIATELY (canRead() short-circuits on exhausted_).
// ===========================================================================
TEST(TCPTransportHuntingGapTest, N5_ExhaustedIsSticky_SubsequentNextLineIsImmediate) {
    test::FakeSocket sock;
    std::deque<std::string> chunks;
    for (auto& c : test::heloHandshakeChunks()) chunks.push_back(c);
    chunks.push_back("ONLY_LINE\r");
    sock.enqueue(kLoopbackIp, test::FakeConnectScript{true, std::move(chunks)});

    auto stop = makeStop();
    auto transport = makeTransport(sock, kLoopbackIp, 3333, stop, silentOutput(),
                                   HuntResilienceConfig{noOpDiscoveryFactory(), {}});
    ASSERT_TRUE(openAgainstSocket(*transport, sock));
    auto first = transport->nextLine();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, "ONLY_LINE");

    // Peer close → recv 0 → nextLine returns nullopt AND sets exhausted_.
    // requestStop bounds any hunt-reconnect attempt so this returns promptly.
    transport->requestStop();
    auto nullopt1 = transport->nextLine();
    EXPECT_FALSE(nullopt1.has_value())
        << "peer close + stop should yield nullopt (and set exhausted_)";

    auto probeStart = std::chrono::steady_clock::now();
    auto nullopt2 = transport->nextLine();
    auto probeEnd = std::chrono::steady_clock::now();
    EXPECT_FALSE(nullopt2.has_value())
        << "sticky exhausted_ must make the second nextLine return nullopt too";
    long probeMs = elapsedMs(probeStart, probeEnd);
    EXPECT_LT(probeMs, 10)
        << "second nextLine took " << probeMs << "ms; exhausted_ short-circuit "
        << "should return in ~0ms (a broken sticky flag would block ~one "
        << "readTimeoutUs poll before returning)";

    stop->reset();
}

#endif // VEHICLE_SIM_HUNTING_ENABLED

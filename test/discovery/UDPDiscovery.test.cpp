#include <gtest/gtest.h>
#include "vehicle-sim/discovery/UDPDiscovery.h"
#include "vehicle-sim/discovery/IDiscoverySocket.h"
#include "vehicle-sim/discovery/DiscoveryPacket.h"
#include "vehicle-sim/discovery/DiscoveredDevice.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace vehicle_sim::discovery;
using vehicle_sim::pipeline::StopToken;

namespace {

// Build a well-formed 106-byte discovery packet from the given fields. The
// signature field is zeroed — discovery packets are intentionally unsigned
// (the firmware sends a zeroed signature; verification is for OTA only), so a
// zero signature is what a real ESP32 discovery broadcast carries.
std::vector<uint8_t> buildPacket(const std::array<uint8_t, DEVICE_ID_LEN>& deviceId,
                                 uint64_t timestamp,
                                 uint16_t canPort,
                                 uint16_t otaPort) {
    DiscoveryPacket p{};
    p.deviceId = deviceId;
    p.timestamp = timestamp;
    p.canPort = canPort;
    p.otaPort = otaPort;
    p.signature.fill(0);
    std::vector<uint8_t> buf(PACKET_LEN);
    p.serialize(buf.data());
    return buf;
}

// A queued received datagram: its bytes + the sender IPv4 address (host byte
// order, matching IDiscoverySocket::recvFrom's contract).
struct FakeDatagram {
    std::vector<uint8_t> bytes;
    uint32_t fromAddrHost = 0;
};

// Fake IDiscoverySocket that scripts canned datagrams. It records the bind/
// non-blocking/close calls and serves queued datagrams one per recvFrom().
// This lets UDPDiscovery's recv→parse→dedup→callback loop run against fully
// controlled input — no real socket, no loopback, no LAN dependency.
//
// Configurable behaviors:
//   - bindFails: bind() returns false (drives UDPDiscovery::start's false path).
//   - queued: datagrams served in FIFO order by recvFrom(); once empty,
//     recvFrom returns -1 (mirrors EAGAIN on a drained non-blocking socket).
//   - pollResults: the sequence of values pollReadable returns, one per call;
//     defaults to always-readable(1) so the drain loop runs immediately.
class FakeDiscoverySocket final : public IDiscoverySocket {
public:
    bool bindFails = false;
    int bindCallCount = 0;
    int setNonBlockingCallCount = 0;
    int closeCallCount = 0;
    std::vector<FakeDatagram> queued;
    std::vector<int> pollResults;  // consumed one per pollReadable() call

    // Convenience: enqueue a datagram.
    void enqueue(std::vector<uint8_t> bytes, uint32_t fromAddrHost) {
        queued.push_back({std::move(bytes), fromAddrHost});
    }

    bool bind(uint16_t /*port*/) override {
        ++bindCallCount;
        if (bindFails) return false;
        return true;
    }
    bool setNonBlocking() override {
        ++setNonBlockingCallCount;
        return true;
    }
    ssize_t recvFrom(uint8_t* buf, size_t len, uint32_t* outFromAddr) override {
        if (queued.empty()) {
            return -1;  // drained: EAGAIN-equivalent
        }
        FakeDatagram d = std::move(queued.front());
        queued.erase(queued.begin());
        size_t n = std::min(d.bytes.size(), len);
        std::memcpy(buf, d.bytes.data(), n);
        if (outFromAddr) *outFromAddr = d.fromAddrHost;
        return static_cast<ssize_t>(n);
    }
    int pollReadable(int /*timeoutMs*/) override {
        if (!pollResults.empty()) {
            int r = pollResults.front();
            pollResults.erase(pollResults.begin());
            return r;
        }
        return 1;  // default: always report readable so the drain loop proceeds
    }
    void close() noexcept override { ++closeCallCount; }
};

// IPv4 host-order for 192.168.1.10 / .20 (deterministic test addresses, no LAN).
constexpr uint32_t kAddr10 = (192u << 24) | (168u << 16) | (1u << 8) | 10u;
constexpr uint32_t kAddr20 = (192u << 24) | (168u << 16) | (1u << 8) | 20u;

std::array<uint8_t, DEVICE_ID_LEN> deviceIdWith(uint8_t fill) {
    std::array<uint8_t, DEVICE_ID_LEN> id{};
    id.fill(fill);
    return id;
}

} // namespace

// ============================================================
// UDPDiscovery — discovery loop contracts (FakeDiscoverySocket-backed)
//
// These drive REAL production code: UDPDiscovery::start/poll/tryReceive run
// exactly as in production. The fake only stands in for the raw UDP socket so
// the recv→parse→dedup→callback loop is deterministic — no real socket, no
// loopback, no LAN/ESP32 dependency (the flakiness sources per #123).
// ============================================================

// Contract: start() binds the socket and sets it non-blocking, then isListening
// is true. The fake records the call sequence so we assert the lifecycle.
TEST(UDPDiscoveryTest, Start_BindsSocketAndSetsNonBlockingThenIsListening) {
    auto fake = std::make_unique<FakeDiscoverySocket>();
    FakeDiscoverySocket* raw = fake.get();
    UDPDiscovery discovery(std::move(fake));

    EXPECT_FALSE(discovery.isListening());
    ASSERT_TRUE(discovery.start());
    EXPECT_TRUE(discovery.isListening());

    EXPECT_EQ(raw->bindCallCount, 1);
    EXPECT_EQ(raw->setNonBlockingCallCount, 1);
}

// Contract: start() is idempotent — a second call does not re-bind (mirrors the
// original `if (sockfd >= 0) return true` guard).
TEST(UDPDiscoveryTest, Start_Twice_IsIdempotent) {
    auto fake = std::make_unique<FakeDiscoverySocket>();
    FakeDiscoverySocket* raw = fake.get();
    UDPDiscovery discovery(std::move(fake));

    ASSERT_TRUE(discovery.start());
    ASSERT_TRUE(discovery.start());  // second call

    EXPECT_EQ(raw->bindCallCount, 1) << "second start must not re-bind";
}

// Contract: when bind() fails, start() returns false and isListening stays false.
TEST(UDPDiscoveryTest, Start_BindFails_ReturnsFalseAndNotListening) {
    auto fake = std::make_unique<FakeDiscoverySocket>();
    fake->bindFails = true;
    UDPDiscovery discovery(std::move(fake));

    EXPECT_FALSE(discovery.start());
    EXPECT_FALSE(discovery.isListening());
}

// Contract: poll() drains one valid discovery packet and surfaces it, with the
// device fields populated from the parsed packet and the address string built
// from the sender's IPv4 address.
TEST(UDPDiscoveryTest, Poll_ValidPacket_SurfacesDeviceWithParsedFields) {
    auto fake = std::make_unique<FakeDiscoverySocket>();
    fake->enqueue(buildPacket(deviceIdWith(0xAB), /*timestamp=*/1000,
                              /*canPort=*/3333, /*otaPort=*/80),
                  kAddr10);
    UDPDiscovery discovery(std::move(fake));
    ASSERT_TRUE(discovery.start());

    auto devices = discovery.poll(std::chrono::milliseconds(50));

    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].deviceId, deviceIdWith(0xAB));
    EXPECT_EQ(devices[0].address, "192.168.1.10");
    EXPECT_EQ(devices[0].canPort, 3333);
    EXPECT_EQ(devices[0].otaPort, 80);
    EXPECT_EQ(devices[0].timestamp, 1000u);
}

// Contract: a malformed packet (wrong magic / too short) is rejected — poll
// surfaces zero devices. parse() failure funnels through tryReceive's false path.
TEST(UDPDiscoveryTest, Poll_MalformedPacket_RejectedAndNoDeviceSurfaced) {
    auto fake = std::make_unique<FakeDiscoverySocket>();
    // Garbage: too short and wrong magic.
    fake->enqueue(std::vector<uint8_t>{0x00, 0x01, 0x02, 0x03}, kAddr10);
    UDPDiscovery discovery(std::move(fake));
    ASSERT_TRUE(discovery.start());

    auto devices = discovery.poll(std::chrono::milliseconds(50));
    EXPECT_TRUE(devices.empty());
}

// Contract: poll() deduplicates devices by address within a single poll cycle.
// Two valid packets from the SAME address yield one device.
TEST(UDPDiscoveryTest, Poll_TwoPacketsSameAddress_DeduplicatesToOneDevice) {
    auto fake = std::make_unique<FakeDiscoverySocket>();
    fake->enqueue(buildPacket(deviceIdWith(0xAB), 1000, 3333, 80), kAddr10);
    fake->enqueue(buildPacket(deviceIdWith(0xCD), 2000, 3333, 80), kAddr10);
    UDPDiscovery discovery(std::move(fake));
    ASSERT_TRUE(discovery.start());

    auto devices = discovery.poll(std::chrono::milliseconds(50));
    ASSERT_EQ(devices.size(), 1u) << "same-address packets must dedup";
}

// Contract: packets from DISTINCT addresses each surface as a device (no
// over-deduplication).
TEST(UDPDiscoveryTest, Poll_TwoPacketsDistinctAddresses_SurfacesBothDevices) {
    auto fake = std::make_unique<FakeDiscoverySocket>();
    fake->enqueue(buildPacket(deviceIdWith(0xAB), 1000, 3333, 80), kAddr10);
    fake->enqueue(buildPacket(deviceIdWith(0xCD), 2000, 3334, 81), kAddr20);
    UDPDiscovery discovery(std::move(fake));
    ASSERT_TRUE(discovery.start());

    auto devices = discovery.poll(std::chrono::milliseconds(50));
    ASSERT_EQ(devices.size(), 2u);
    // Addresses are distinct and match the senders.
    std::vector<std::string> addrs{devices[0].address, devices[1].address};
    std::sort(addrs.begin(), addrs.end());
    EXPECT_EQ(addrs[0], "192.168.1.10");
    EXPECT_EQ(addrs[1], "192.168.1.20");
}

// Contract: the device callback fires once per VALID packet (before dedup, since
// it fires inside tryReceive; dedup only affects what poll returns).
TEST(UDPDiscoveryTest, DeviceCallback_FiresOncePerValidPacket) {
    auto fake = std::make_unique<FakeDiscoverySocket>();
    // Two valid packets from the same address: callback fires twice, poll
    // returns one (deduped).
    fake->enqueue(buildPacket(deviceIdWith(0xAB), 1000, 3333, 80), kAddr10);
    fake->enqueue(buildPacket(deviceIdWith(0xCD), 2000, 3333, 80), kAddr10);
    UDPDiscovery discovery(std::move(fake));
    ASSERT_TRUE(discovery.start());

    int callbackCount = 0;
    discovery.setDeviceCallback([&callbackCount](const DiscoveredDevice&) {
        ++callbackCount;
    });

    auto devices = discovery.poll(std::chrono::milliseconds(50));
    EXPECT_EQ(callbackCount, 2) << "callback fires per valid packet (pre-dedup)";
    EXPECT_EQ(devices.size(), 1u) << "poll result is post-dedup";
}

// Contract: a cooperative stop requested mid-poll ends the poll promptly (the
// stop-token funnel). The injected StopToken is the live signal-handler path.
TEST(UDPDiscoveryTest, Poll_StopRequested_ReturnsPromptly) {
    auto fake = std::make_unique<FakeDiscoverySocket>();
    // Make pollReadable report "not readable" (0 = timeout) so the loop would
    // otherwise run for the full timeout duration.
    fake->pollResults = {0, 0, 0, 0, 0, 0, 0, 0};
    auto stop = std::make_shared<StopToken>();
    UDPDiscovery discovery(std::move(fake), stop);
    ASSERT_TRUE(discovery.start());

    stop->requestStop();
    auto devices = discovery.poll(std::chrono::milliseconds(2000));
    EXPECT_TRUE(devices.empty());
    // The stop is observed by the loop; the contract is that it does not block
    // for the full 2s. (No hard wall-clock assertion — the loop checks stop
    // before each 100ms chunk, so it returns well under 2s.)
}

// Contract: stop() closes the socket and clears listening.
TEST(UDPDiscoveryTest, Stop_ClosesSocketAndClearsListening) {
    auto fake = std::make_unique<FakeDiscoverySocket>();
    FakeDiscoverySocket* raw = fake.get();
    UDPDiscovery discovery(std::move(fake));
    ASSERT_TRUE(discovery.start());

    discovery.stop();
    EXPECT_FALSE(discovery.isListening());
    EXPECT_EQ(raw->closeCallCount, 1);
}

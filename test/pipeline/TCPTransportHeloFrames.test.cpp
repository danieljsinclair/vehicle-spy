// TCPTransportHeloFrames.test.cpp — TDD spec for the CAN-frame-tolerant HELO handshake
// (readLineSkippingFrames). The ESP32 firmware streams raw CAN data frames immediately
// after AUTH; the client's ATI/ATHELO reads must skip those frame lines and find the
// command reply beneath them.
//
// These are CHARACTERISATION tests: they lock the externally-observable contract
// (handshake succeeds when frames are interleaved; fails with the [tcp] diagnostic
// when no ACK ever arrives) using a scripted FakeSocket (no real network).

#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/ITransportOutput.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/pipeline/FakeSocket.h"
#include "vehicle-sim/util/IClock.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace vehicle_sim::pipeline;
namespace util = vehicle_sim::util;

namespace {

// Capturing output sink — asserts the named [tcp] diagnostic fires on failure.
class CapturingOutput final : public ITransportOutput {
public:
    void out(const std::string& msg) override {
        std::lock_guard<std::mutex> lk(mu_);
        outLines_.push_back(msg);
    }
    void err(const std::string& msg) override {
        std::lock_guard<std::mutex> lk(mu_);
        errLines_.push_back(msg);
    }
    std::vector<std::string> outLines() const {
        std::lock_guard<std::mutex> lk(mu_);
        return outLines_;
    }
    std::vector<std::string> errLines() const {
        std::lock_guard<std::mutex> lk(mu_);
        return errLines_;
    }

private:
    mutable std::mutex mu_;
    std::vector<std::string> outLines_;
    std::vector<std::string> errLines_;
};

// Build a TCPTransport wired to `sock` and `out`. Defaults to the "raw" protocol
// with FakeClock (deterministic pacing) and the production timeout values.
std::unique_ptr<TCPTransport> makeTransport(test::FakeSocket& sock,
                                            std::shared_ptr<CapturingOutput> out,
                                            const std::string& protocol = "raw") {
    return std::make_unique<TCPTransport>(
        TransportEndpoint{"127.0.0.1", 3333, protocol},
        out, TcpReadTiming{1000, -1, 1}, std::make_shared<StopToken>(),
        HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
        std::shared_ptr<ISocket>(&sock, [](ISocket*) {}));
}

// Drive open() to completion and return whether it succeeded. open() calls
// connectUntilUp() -> connectAndAuth() -> performHeloHandshake().
bool runOpen(TCPTransport& t) {
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = t.open(); });
    th.join();
    return opened.load();
}

// Well-formed ACK prefix shared by happy-path cases.
constexpr const char* kAckHead = "ACK DEVICE=ESP32-CAN FIRMWARE=0.1 DEVICEID=";

} // namespace

// ── CAN frames interleaved with ATI and ATHELO: handshake succeeds ──────────
//
// Scripted stream after AUTH OK:
//   [CAN frame] [CAN frame]  ← firmware streams frames immediately post-AUTH
//   ESP32 CAN Bridge v0.1\r>  ← ATI response (after discarding frames)
//   [CAN frame]               ← more frames during ATHELO wait
//   ACK DEVICE=... DEVICEID=<32-hex>\r\r>  ← HELO ACK
//
// The helper must skip all frame lines and deliver both the ATI info line and
// the ACK line so parseHeloAck() succeeds and deviceIdHex_ is populated.
TEST(TCPTransportHeloFrames, InterleavedCanFrames_HandshakeSucceeds) {
    test::FakeSocket sock;

    // Recv script: AUTH OK, then a burst of CAN frames, then ATI banner, more
    // frames, and finally a well-formed HELO ACK.
    std::deque<std::string> chunks;
    chunks.push_back("OK\r");                                 // AUTH response
    chunks.push_back("7E8 03 41 00 FF FF FF\r");              // CAN frame (post-AUTH burst)
    chunks.push_back("7E0 02 41 05 FF\r");                    // another CAN frame
    chunks.push_back("ESP32 CAN Bridge v0.1\r>");             // ATI response
    chunks.push_back("7E8 06 41 0C 1A 00 00 00\r");          // CAN frame (during ATHELO wait)
    chunks.push_back(std::string(kAckHead) +
                     "00112233445566778899AABBCCDDEEFF" +
                     "\r\r>");                               // HELO ACK
    sock.enqueue("127.0.0.1", test::FakeConnectScript{true, std::move(chunks)});

    auto out = std::make_shared<CapturingOutput>();
    auto t = makeTransport(sock, out);
    ASSERT_TRUE(runOpen(*t));
    EXPECT_TRUE(t->isOpen());
    EXPECT_EQ(t->getDeviceId(), "00112233445566778899AABBCCDDEEFF");

    // No diagnostic errors on the happy path.
    EXPECT_TRUE(out->errLines().empty())
        << "expected no [tcp] errors on a successful frame-interleaved handshake; got: "
        << testing::PrintToString(out->errLines());
}

// ── Frames only, no ACK: handshake times out and fails ──────────────────────
//
// After AUTH OK the firmware sends nothing but CAN frames (no ATI, no ACK).
// Each recv() returns a CAN frame (discarded); once chunks are exhausted the
// FakeSocket signals EOF (recv returns 0) and readLineSkippingFrames returns
// std::nullopt. The ATHELO loop terminates with no ACK and the existing
// "[tcp] HELO pre-flight: no response to ATHELO" diagnostic fires.
TEST(TCPTransportHeloFrames, FramesOnly_NoAck_HandshakeFails) {
    test::FakeSocket sock;

    // Recv script: AUTH OK then CAN frames only — no ATI, no ACK.
    std::deque<std::string> chunks;
    chunks.push_back("OK\r");
    chunks.push_back("7E8 03 41 00 FF FF FF\r");
    chunks.push_back("7E0 02 41 05 FF\r");
    chunks.push_back("7E8 06 41 0C 1A 00 00 00\r");
    sock.enqueue("127.0.0.1", test::FakeConnectScript{true, std::move(chunks)});

    auto out = std::make_shared<CapturingOutput>();
    auto t = makeTransport(sock, out);
    EXPECT_FALSE(runOpen(*t));
    // The helper returns std::nullopt as soon as the ATI read times out (the
    // script has no ATI response at all — only CAN frames then EOF), so the
    // failure surfaces as "no response to ATI". The connectUntilUp retry loop
    // then exhausts its budget and the final connect error follows.
    EXPECT_TRUE(out->errLines().size() >= 1)
        << "expected at least one [tcp] diagnostic on failed handshake; got: "
        << testing::PrintToString(out->errLines());
    EXPECT_TRUE(
        [&] {
            for (const auto& line : out->errLines()) {
                if (line.find("no response to ATI") != std::string::npos) return true;
            }
            return false;
        }())
        << "expected 'no response to ATI' diagnostic; got: "
        << testing::PrintToString(out->errLines());
}

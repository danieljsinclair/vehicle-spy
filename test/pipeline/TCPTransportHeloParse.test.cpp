#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/ITransportOutput.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/pipeline/FakeSocket.h"
#include "vehicle-sim/util/IClock.h"

#include <atomic>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <thread>

using namespace vehicle_sim::pipeline;
namespace util = vehicle_sim::util;

// ============================================================================
// SOCKET-FREE-NET SAFETY NET for the S3776 (cc=35) candidate
// `TCPTransport::sendHeloAndParseAck` (TCPTransport.cpp).
//
// That method does two things: (1) socket I/O with a live device (ATI / ATHELO
// + recv loop), and (2) PURE, DETERMINISTIC, SOCKET-FREE parsing: validate the
// ACK string shape, extract the 32-hex device id, hex-decode it to 16 bytes.
//
// We drive the REAL parse through the one honest, deterministic seam: a
// scripted FakeSocket whose ACK line we fully control (no real socket, no real
// loopback server — the fast-test hard requirement). open() performs the
// genuine HELO handshake (real validation code against scripted bytes), and the
// observable OUTCOME is surfaced via getDeviceId() (which re-encodes the parsed
// 16 bytes via "%02X") plus open()/isOpen() success-or-failure.
//
// These are CHARACTERISATION tests: they lock the externally-observable contract
// of the validation (length = exactly 32 hex, hex-decode, CRLF/prompt strip,
// uppercase canonicalisation) so the upcoming S3776 extract-and-simplify refactor
// cannot silently change parse semantics.
// ============================================================================

namespace {

// Result of one controlled HELO handshake.
struct HeloResult {
    bool opened = false;
    std::string deviceId;  // getDeviceId() after open()
    std::unique_ptr<TCPTransport> transport;
    std::shared_ptr<StopToken> stop;
    test::FakeSocket sock;
};

// Speak AUTH/ATI/ATHELO, then emit `ack` (the raw ACK bytes, including any
// trailing terminator the test wants). If closeAfterAck is true the script ends
// after the ACK (peer close on the next recv), which makes rejection cases
// terminate promptly instead of stalling on the 500ms SO_RCVTIMEO.
static HeloResult runHeloHandshake(const std::string& ack, bool closeAfterAck) {
    HeloResult r;
    r.stop = std::make_shared<StopToken>();

    std::deque<std::string> chunks;
    chunks.push_back("OK\r");
    chunks.push_back("ESP32 CAN Bridge v0.1\r>");
    chunks.push_back(ack);
    r.sock.enqueue("127.0.0.1", test::FakeConnectScript{true, std::move(chunks)});

    r.transport = std::make_unique<TCPTransport>(
        TransportEndpoint{"127.0.0.1", 3333, "raw"},
        std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 500}, r.stop,
        HuntResilienceConfig{}, std::make_shared<util::FakeClock>(),
        std::shared_ptr<ISocket>(&r.sock, [](ISocket*) {}));

    std::atomic<bool> opened{false};
    std::thread th([&] { opened = r.transport->open(); });
    th.join();
    r.opened = opened.load();
    if (r.opened) {
        r.deviceId = r.transport->getDeviceId();
    }
    return r;
}

// Tidy teardown helper: stop the transport and drain one line.
static void finish(HeloResult& r) {
    if (r.stop) r.stop->requestStop();
    if (r.transport) r.transport->nextLine();
    if (r.stop) r.stop->reset();
}

// Well-formed ACK prefix shared by the happy-path cases.
constexpr const char* kAckHead = "ACK DEVICE=ESP32-CAN FIRMWARE=0.1 DEVICEID=";

} // namespace

// --- HAPPY PATH: valid 32-hex decodes to the exact byte sequence ---------------

TEST(TCPTransportHeloParseContract, Valid32Hex_RoundTripsToDeviceId) {
    HeloResult r = runHeloHandshake(
        std::string(kAckHead) + "00112233445566778899AABBCCDDEEFF" + "\r\r>", false);
    EXPECT_TRUE(r.opened);
    EXPECT_TRUE(r.transport && r.transport->isOpen());
    EXPECT_EQ(r.deviceId, "00112233445566778899AABBCCDDEEFF");
    finish(r);
}

TEST(TCPTransportHeloParseContract, Valid32Hex_AllFfAndAllZero) {
    {
        HeloResult r = runHeloHandshake(
            std::string(kAckHead) + "00000000000000000000000000000000" + "\r\r>", false);
        EXPECT_TRUE(r.opened);
        EXPECT_EQ(r.deviceId, "00000000000000000000000000000000");
        finish(r);
    }
    {
        HeloResult r = runHeloHandshake(
            std::string(kAckHead) + "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF" + "\r\r>", false);
        EXPECT_TRUE(r.opened);
        EXPECT_EQ(r.deviceId, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
        finish(r);
    }
}

TEST(TCPTransportHeloParseContract, LowercaseHex_AcceptedAndCanonicalisedToUpper) {
    // The decoder accepts a-f; performHeloHandshake re-emits via "%02X" (upper),
    // so lowercase input must surface as the uppercase device id.
    HeloResult r = runHeloHandshake(
        std::string(kAckHead) + "00112233445566778899aabbccddeeff" + "\r\r>", false);
    EXPECT_TRUE(r.opened);
    EXPECT_EQ(r.deviceId, "00112233445566778899AABBCCDDEEFF");
    finish(r);
}

TEST(TCPTransportHeloParseContract, ThirtyOneHex_Rejected) {
    // One short of the required 32 -> handshake must fail.
    HeloResult r = runHeloHandshake(
        std::string(kAckHead) + "00112233445566778899AABBCCDDEEFF" + "0" + "\r\r>", true);
    EXPECT_FALSE(r.opened);
    EXPECT_TRUE(r.transport && !r.transport->isOpen());
    finish(r);
}

TEST(TCPTransportHeloParseContract, ThirtyThreeHex_Rejected) {
    // One too many -> handshake must fail.
    HeloResult r = runHeloHandshake(
        std::string(kAckHead) + "00112233445566778899AABBCCDDEEFF" + "00" + "\r\r>", true);
    EXPECT_FALSE(r.opened);
    finish(r);
}

TEST(TCPTransportHeloParseContract, NonHexChars_Rejected) {
    // 'G' and 'Z' are not valid hex; the per-byte parse must fail.
    HeloResult r = runHeloHandshake(
        std::string(kAckHead) + "00112233445566778899AABBCCDDEEZZ" + "\r\r>", true);
    EXPECT_FALSE(r.opened);
    finish(r);
}

TEST(TCPTransportHeloParseContract, MissingDeviceIdToken_Rejected) {
    // An ACK with no "DEVICEID=" field must not be accepted.
    HeloResult r = runHeloHandshake(
        std::string("ACK DEVICE=ESP32-CAN FIRMWARE=0.1\r\r>"), true);
    EXPECT_FALSE(r.opened);
    finish(r);
}

TEST(TCPTransportHeloParseContract, MissingAckDevicePrefix_Rejected) {
    // The producer must emit the "ACK DEVICE=" prefix; a bare DEVICEID line is
    // not a valid ACK.
    HeloResult r = runHeloHandshake(
        std::string("DEVICEID=00112233445566778899AABBCCDDEEFF\r\r>"), true);
    EXPECT_FALSE(r.opened);
    finish(r);
}

TEST(TCPTransportHeloParseContract, TrailingSpaceStripped) {
    // A single trailing space before the prompt is stripped.
    HeloResult r = runHeloHandshake(
        std::string(kAckHead) + "00112233445566778899AABBCCDDEEFF" + " \r\r>", false);
    EXPECT_TRUE(r.opened);
    EXPECT_EQ(r.deviceId, "00112233445566778899AABBCCDDEEFF");
    finish(r);
}

TEST(TCPTransportHeloParseContract, TrailingNewlineStripped) {
    // A trailing '\n' is stripped, leaving a clean 32-hex id.
    HeloResult r = runHeloHandshake(
        std::string(kAckHead) + "00112233445566778899AABBCCDDEEFF" + "\n", false);
    EXPECT_TRUE(r.opened);
    EXPECT_EQ(r.deviceId, "00112233445566778899AABBCCDDEEFF");
    finish(r);
}

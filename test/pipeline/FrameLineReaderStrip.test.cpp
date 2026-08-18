// FrameLineReaderStrip.test.cpp — TDD spec for the leading-`>` prompt strip
// inside TCPTransport::readLineSkippingFrames() (gap d).
//
// The ESP32 firmware emits a bare `>` as the trailing prompt terminator after
// each reply ("<response>\r\r>"). Because readSocketIntoPending appends the
// next chunk to any leftover bytes, a subsequent recv can leave the shared
// pending_ buffer opening with a stray `>`. readLineSkippingFrames strips
// that leading `>` (and surrounding whitespace) before the line is classified
// so the prompt byte never leaks into the post-handshake stream.
//
// CHARACTERISATION tests: pin the strip contract directly through the public
// readLineSkippingFrames() surface (not via the implicit HELO-handshake
// integration path). Uses FakeSocket to script exact recv byte sequences.

#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/ITransportOutput.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/pipeline/FakeSocket.h"
#include "vehicle-sim/util/IClock.h"

#include <atomic>
#include <deque>
#include <memory>
#include <thread>

using namespace vehicle_sim::pipeline;
namespace util = vehicle_sim::util;

static std::shared_ptr<StopToken> g_testStop = std::make_shared<StopToken>();

// Open a transport against a fully-scripted handshake and return it so a
// test can call readLineSkippingFrames() directly.
struct ReadyForReadLine {
    test::FakeSocket sock;
    std::shared_ptr<util::FakeClock> clock = std::make_shared<util::FakeClock>();
    std::unique_ptr<TCPTransport> transport;
};

static std::unique_ptr<ReadyForReadLine> openReadyForReadLine(
    std::deque<std::string> postHandshakeChunks = {}) {
    auto ctx = std::make_unique<ReadyForReadLine>();
    g_testStop->reset();
    std::deque<std::string> chunks = test::heloHandshakeChunks();
    for (auto& c : postHandshakeChunks) chunks.push_back(std::move(c));
    ctx->sock.enqueue("127.0.0.1", test::FakeConnectScript{true, std::move(chunks)});
    ctx->transport = std::make_unique<TCPTransport>(
        TransportEndpoint{"127.0.0.1", 3333, "raw"},
        std::make_shared<StdOut>(), TcpReadTiming{1000, -1, 1}, g_testStop,
        HuntResilienceConfig{}, ctx->clock,
        std::shared_ptr<ISocket>(&ctx->sock, [](ISocket*) {}));
    std::atomic<bool> opened{false};
    std::thread th([&] { opened = ctx->transport->open(); });
    th.join();
    if (!opened.load()) return nullptr;
    return ctx;
}

// ── Leading `>` on a text reply is stripped ─────────────────────────────────
//
// Script: after the standard HELO handshake, the firmware sends a line that
// opens with `>` (the trailing prompt from the ACK banner leaked into the
// next recv). readLineSkippingFrames must strip the `>` and return the clean
// text reply.

TEST(FrameLineReaderStripTest, LeadingGreaterThan_StrippedFromTextReply) {
    // After the handshake, feed a line that opens with `>` simulating the
    // prompt byte bleeding into the next chunk.
    auto ctx = openReadyForReadLine({">OK\r"});
    ASSERT_TRUE(ctx);
    g_testStop->reset();

    // readLineSkippingFrames is public; call it directly to pin the strip.
    std::optional<std::string> line = ctx->transport->readLineSkippingFrames();
    ASSERT_TRUE(line.has_value())
        << "readLineSkippingFrames must return a line, not nullopt, when the "
           "only issue is a leading `>` prompt byte.";
    EXPECT_EQ(*line, "OK")
        << "Leading `>` (and any surrounding whitespace) must be stripped from "
           "the reply; expected 'OK', got: "
        << *line;

    g_testStop->requestStop();
    ctx->transport->nextLine(); // drain any residual
    g_testStop->reset();
}

// ── Leading `>` on a CAN frame is stripped then frame is discarded ───────────
//
// A CAN frame line whose leading `>` came from a prompt bleed must still be
// classified as a frame (after stripping) and discarded by
// readLineSkippingFrames. The function must then continue reading for the
// next non-frame line.

TEST(FrameLineReaderStripTest, LeadingGreaterThanOnCanFrame_StrippedThenDiscarded) {
    // Feed a CAN-frame line with a leading `>` bleed, then a real ATI banner.
    auto ctx = openReadyForReadLine({
        ">7E8 03 41 00 FF FF FF\r",   // CAN frame with leading `>` (prompt bleed)
        "ESP32 CAN Bridge v0.1\r",    // ATI banner (no trailing `>` this time)
    });
    ASSERT_TRUE(ctx);
    g_testStop->reset();

    // First call: the frame-prefixed line must be stripped and discarded;
    // the function must continue and return the ATI banner on the same call.
    std::optional<std::string> line = ctx->transport->readLineSkippingFrames();
    ASSERT_TRUE(line.has_value())
        << "readLineSkippingFrames must skip the stripped CAN frame and return "
           "the next non-frame line in the same call.";
    EXPECT_EQ(*line, "ESP32 CAN Bridge v0.1");

    g_testStop->requestStop();
    ctx->transport->nextLine();
    g_testStop->reset();
}

// ── Whitespace + `>` combo: fully cleaned ────────────────────────────────────
//
// The strip targets the character set `">\r\n \t"`. Verify a line that opens
// with a mix of those characters resolves to the underlying text.

TEST(FrameLineReaderStripTest, WhitespaceAndGreaterThan_FullyCleaned) {
    auto ctx = openReadyForReadLine({">  OK\r"});
    ASSERT_TRUE(ctx);
    g_testStop->reset();

    std::optional<std::string> line = ctx->transport->readLineSkippingFrames();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "OK")
        << "Leading `>` plus whitespace must be fully stripped.";

    g_testStop->requestStop();
    ctx->transport->nextLine();
    g_testStop->reset();
}

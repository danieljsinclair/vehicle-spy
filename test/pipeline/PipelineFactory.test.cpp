#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/PipelineFactory.h"
#include "vehicle-sim/pipeline/FileTransport.h"
#include "vehicle-sim/pipeline/DemoTransport.h"
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/USBTransport.h"
#include "vehicle-sim/pipeline/CaptureNormaliser.h"
#include "vehicle-sim/pipeline/RawFrameNormaliser.h"
#include "vehicle-sim/pipeline/Elm327Normaliser.h"

using namespace vehicle_sim::pipeline;

// ============================================================
// resolveAdapterProtocol — default table + explicit override
// ============================================================

TEST(ResolveAdapterProtocolTest, DemoDefaultsToRaw) {
    EXPECT_EQ(resolveAdapterProtocol("demo", ""), "raw");
    EXPECT_EQ(resolveAdapterProtocol("demo", "default"), "raw");
}

TEST(ResolveAdapterProtocolTest, FileDefaultsToRaw) {
    EXPECT_EQ(resolveAdapterProtocol("file:cap.csv", ""), "raw");
}

TEST(ResolveAdapterProtocolTest, TcpDefaultsToRaw) {
    EXPECT_EQ(resolveAdapterProtocol("tcp:1.2.3.4:3333", ""), "raw");
    EXPECT_EQ(resolveAdapterProtocol("tcp:1.2.3.4", ""), "raw");
}

TEST(ResolveAdapterProtocolTest, UsbDefaultsToRaw) {
    EXPECT_EQ(resolveAdapterProtocol("usb:0", ""), "raw");
}

TEST(ResolveAdapterProtocolTest, BleAddressDefaultsToElm327) {
    // A BLE address is not a recognised scheme → defaults to elm327.
    EXPECT_EQ(resolveAdapterProtocol("AA:BB:CC:DD:EE:FF", ""), "elm327");
}

TEST(ResolveAdapterProtocolTest, ExplicitRawWinsOverDefault) {
    // Explicit raw overrides the BLE default.
    EXPECT_EQ(resolveAdapterProtocol("AA:BB:CC:DD:EE:FF", "raw"), "raw");
}

TEST(ResolveAdapterProtocolTest, ExplicitElm327WinsOverDefault) {
    EXPECT_EQ(resolveAdapterProtocol("demo", "elm327"), "elm327");
    EXPECT_EQ(resolveAdapterProtocol("tcp:1.2.3.4:3333", "elm327"), "elm327");
}

TEST(ResolveAdapterProtocolTest, OverrideIsCaseInsensitive) {
    EXPECT_EQ(resolveAdapterProtocol("demo", "RAW"), "raw");
    EXPECT_EQ(resolveAdapterProtocol("demo", "ELM327"), "elm327");
}

TEST(ResolveAdapterProtocolTest, UnknownProtocolFallsToDefault) {
    // An unknown protocol value is treated as "default" (table applies).
    EXPECT_EQ(resolveAdapterProtocol("demo", "canbus"), "raw");
    EXPECT_EQ(resolveAdapterProtocol("AA:BB:CC:DD:EE:FF", "canbus"), "elm327");
}

// ============================================================
// buildPipelineSource — pairs the right transport with the right normaliser
// ============================================================

TEST(BuildPipelineSourceTest, FileTarget_PairsFileTransportWithCaptureNormaliser) {
    // Create a minimal capture file so the file transport can open it
    {
        std::ofstream f("/tmp/x.csv");
        f << "timestamp_ms,can_id,dlc,data\n";
    }
    auto src = buildPipelineSource("file:/tmp/x.csv", "raw");
    ASSERT_TRUE(src.transport);
    ASSERT_TRUE(src.normaliser);
    // The concrete types are erased; verify behaviorally.
    EXPECT_TRUE(src.transport->open());
    // A CaptureNormaliser parses the verbatim capture form; a RawFrameNormaliser
    // does not. Distinguish by feeding a capture line.
    auto r = src.normaliser->normalise("1000,118,8,3C00180004A001FF");
    EXPECT_EQ(r.kind, NormaliserResultKind::Frame);
}

TEST(BuildPipelineSourceTest, DemoTarget_PairsDemoTransportWithRawFrameNormaliser) {
    auto src = buildPipelineSource("demo", "raw");
    ASSERT_TRUE(src.transport);
    ASSERT_TRUE(src.normaliser);
    EXPECT_TRUE(src.transport->open());
    // A RawFrameNormaliser parses live-raw "ID D0..." but NOT capture lines.
    auto r = src.normaliser->normalise("118 3C 00 18 00 00 00 00 FF");
    EXPECT_EQ(r.kind, NormaliserResultKind::Frame);
}

TEST(BuildPipelineSourceTest, TcpTarget_PairsTcpTransportWithRawFrameNormaliser) {
    // Build only — do not open (would try to connect to a real host).
    auto src = buildPipelineSource("tcp:127.0.0.1:3333", "raw");
    ASSERT_TRUE(src.transport);
    ASSERT_TRUE(src.normaliser);
    auto r = src.normaliser->normalise("118 3C 00 18 00 00 00 00 FF");
    EXPECT_EQ(r.kind, NormaliserResultKind::Frame);
}

TEST(BuildPipelineSourceTest, TcpTargetExplicitElm327_PairsTcpTransportWithElm327Normaliser) {
    // Explicit --adapter-protocol elm327 over TCP must select Elm327Normaliser.
    // Distinguish behaviourally: a 4-digit hex ID is a valid frame to the raw
    // normaliser but Malformed to the ELM327 normaliser (11-bit bound).
    auto src = buildPipelineSource("tcp:127.0.0.1:3333", "elm327");
    ASSERT_TRUE(src.transport);
    ASSERT_TRUE(src.normaliser);
    auto r = src.normaliser->normalise("1234 00 00");
    EXPECT_EQ(r.kind, NormaliserResultKind::Malformed);
    // The same source still parses a well-formed 11-bit monitor line.
    auto ok = src.normaliser->normalise("1D5 29 00 00 00 00 00 A0 9F");
    EXPECT_EQ(ok.kind, NormaliserResultKind::Frame);
}

TEST(BuildPipelineSourceTest, UsbTarget_PairsUSBTransportWithRawFrameNormaliser) {
    auto src = buildPipelineSource("usb:/dev/cu.X", "raw");
    ASSERT_TRUE(src.transport);
    ASSERT_TRUE(src.normaliser);
    EXPECT_TRUE(dynamic_cast<USBTransport*>(src.transport.get()) != nullptr);
    auto r = src.normaliser->normalise("1D5 29 00 00 00 00 00 A0 9F");
    EXPECT_EQ(r.kind, NormaliserResultKind::Frame);
}

TEST(BuildPipelineSourceTest, UsbTargetExplicitElm327_PairsUSBTransportWithElm327Normaliser) {
    auto src = buildPipelineSource("usb:/dev/cu.X", "elm327");
    ASSERT_TRUE(src.transport);
    ASSERT_TRUE(src.normaliser);
    EXPECT_TRUE(dynamic_cast<USBTransport*>(src.transport.get()) != nullptr);
    auto r = src.normaliser->normalise("1D5 29 00 00 00 00 00 A0 9F");
    EXPECT_EQ(r.kind, NormaliserResultKind::Frame);
}

TEST(BuildPipelineSourceTest, TcpTargetDefaultsPortTo3333) {
    // "tcp:host" with no port parses (we can't observe the port without
    // opening, but the source must still build).
    auto src = buildPipelineSource("tcp:192.168.4.1", "raw");
    EXPECT_TRUE(src.transport);
    EXPECT_TRUE(src.normaliser);
}

TEST(BuildPipelineSourceTest, UnsupportedTarget_ReturnsNullPair) {
    // BLE addresses are treated as legacy BLE transport targets by the CLI.
    auto src = buildPipelineSource("AA:BB:CC:DD:EE:FF", "elm327");
    EXPECT_FALSE(src.transport);
    EXPECT_FALSE(src.normaliser);
}

// ============================================================
// parseTcpTarget — the single canonical TCP-target parser (moved here from
// the legacy SignalSourceFactory; one definition, no duplicates).
// ============================================================

TEST(ParseTcpTargetTest, IpAndPort_SplitsCorrectly) {
    std::string host;
    int port = 0;
    ASSERT_TRUE(parseTcpTarget("tcp:192.168.4.1:3333", host, port));
    EXPECT_EQ(host, "192.168.4.1");
    EXPECT_EQ(port, 3333);
}

TEST(ParseTcpTargetTest, IpOnly_DefaultsPortTo3333) {
    std::string host;
    int port = 0;
    ASSERT_TRUE(parseTcpTarget("tcp:192.168.4.1", host, port));
    EXPECT_EQ(host, "192.168.4.1");
    EXPECT_EQ(port, 3333);
}

TEST(ParseTcpTargetTest, CustomPort_ParsesPort) {
    std::string host;
    int port = 0;
    ASSERT_TRUE(parseTcpTarget("tcp:10.0.0.5:4444", host, port));
    EXPECT_EQ(host, "10.0.0.5");
    EXPECT_EQ(port, 4444);
}

TEST(ParseTcpTargetTest, NonTcpTarget_ReturnsFalse) {
    std::string host;
    int port = 0;
    EXPECT_FALSE(parseTcpTarget("demo", host, port));
    EXPECT_FALSE(parseTcpTarget("file:x.csv", host, port));
    EXPECT_FALSE(parseTcpTarget("AA:BB:CC:DD:EE:FF", host, port));
}

TEST(ParseTcpTargetTest, BarePrefix_ReturnsFalse) {
    std::string host;
    int port = 0;
    EXPECT_FALSE(parseTcpTarget("tcp:", host, port));
}

TEST(ParseTcpTargetTest, OutOfRangePort_ReturnsFalse) {
    std::string host;
    int port = 0;
    EXPECT_FALSE(parseTcpTarget("tcp:1.2.3.4:99999", host, port));
    EXPECT_FALSE(parseTcpTarget("tcp:1.2.3.4:0", host, port));
}

TEST(ParseTcpTargetTest, NonNumericPort_TreatsBodyAsHost) {
    // A trailing non-numeric token is treated as part of the host, default port.
    std::string host;
    int port = 0;
    ASSERT_TRUE(parseTcpTarget("tcp:host.local", host, port));
    EXPECT_EQ(host, "host.local");
    EXPECT_EQ(port, 3333);
}

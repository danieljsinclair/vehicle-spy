#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/PipelineFactory.h"
#include "vehicle-sim/pipeline/DemoTransport.h"
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/USBTransport.h"

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
    EXPECT_EQ(resolveAdapterProtocol("AA:BB:CC:DD:EE:FF", ""), "elm327");
}

TEST(ResolveAdapterProtocolTest, ExplicitRawWinsOverDefault) {
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
    EXPECT_EQ(resolveAdapterProtocol("demo", "canbus"), "raw");
    EXPECT_EQ(resolveAdapterProtocol("AA:BB:CC:DD:EE:FF", "canbus"), "elm327");
}

// ============================================================
// buildPipelineSource — pairs the right transport with the right kind
// ============================================================

TEST(BuildPipelineSourceTest, DemoTarget_PairsDemoTransport) {
    auto src = buildPipelineSource("demo", "raw");
    ASSERT_TRUE(src.transport);
    EXPECT_TRUE(src.transport->open());
    EXPECT_TRUE(dynamic_cast<DemoTransport*>(src.transport.get()) != nullptr);
}

TEST(BuildPipelineSourceTest, TcpTarget_PairsTcpTransport) {
    auto src = buildPipelineSource("tcp:127.0.0.1:3333", "raw");
    ASSERT_TRUE(src.transport);
    EXPECT_TRUE(dynamic_cast<TCPTransport*>(src.transport.get()) != nullptr);
}

TEST(BuildPipelineSourceTest, UsbTarget_PairsUSBTransport) {
    auto src = buildPipelineSource("usb:/dev/cu.X", "raw");
    ASSERT_TRUE(src.transport);
    EXPECT_TRUE(dynamic_cast<USBTransport*>(src.transport.get()) != nullptr);
}

TEST(BuildPipelineSourceTest, TcpTargetDefaultsPortTo3333) {
    auto src = buildPipelineSource("tcp:192.168.4.1", "raw");
    EXPECT_TRUE(src.transport);
}

TEST(BuildPipelineSourceTest, UnsupportedTarget_ReturnsNullTransport) {
    auto src = buildPipelineSource("AA:BB:CC:DD:EE:FF", "elm327");
    EXPECT_FALSE(src.transport);
}

TEST(BuildPipelineSourceTest, FileTargetReturnsNullTransport) {
    // File replay is now handled by ReplayRunContext + BinaryFileSource, not
    // by the live-source factory. The factory returns null for file targets.
    auto src = buildPipelineSource("file:/tmp/x.csv", "raw");
    EXPECT_FALSE(src.transport);
}

// ============================================================
// parseTcpTarget — single canonical TCP-target parser
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
    std::string host;
    int port = 0;
    ASSERT_TRUE(parseTcpTarget("tcp:host.local", host, port));
    EXPECT_EQ(host, "host.local");
    EXPECT_EQ(port, 3333);
}

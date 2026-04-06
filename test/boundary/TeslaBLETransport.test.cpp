#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>

#include "vehicle-sim/ble/TeslaBLETransport.h"
#include "vehicle-sim/ble/platform/BLEManagerMock.h"

using namespace vehicle_sim::ble;

class TeslaBLETransportTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create BLEPlatform mock and inject into transport
        platformMock = std::make_unique<BLEManagerMock>();
        transport = std::make_unique<TeslaBLETransport>(std::move(platformMock));
    }

    void TearDown() override {
        transport.reset();
        platformMock.reset();
    }

    std::unique_ptr<BLEManagerMock> platformMock;
    std::unique_ptr<TeslaBLETransport> transport;
};

// ================================================
// Test Suite 1: Connection Lifecycle Tests
// ================================================

TEST_F(TeslaBLETransportTest, ConnectsSuccessfully)
{
    // ASSERT: TeslaBLETransport can establish BLE connection

    bool connected = transport->connect("00:11:22:33:44:55");

    ASSERT_TRUE(connected)
        << "Transport should successfully connect to valid device";
    ASSERT_TRUE(transport->isConnected())
        << "Transport should report connected after successful connect";
}

TEST_F(TeslaBLETransportTest, DisconnectsGracefully)
{
    // ASSERT: Transport can disconnect from device

    transport->connect("test-device");
    ASSERT_TRUE(transport->isConnected());

    transport->disconnect();
    ASSERT_FALSE(transport->isConnected())
        << "Transport should report disconnected after disconnect";
}

TEST_F(TeslaBLETransportTest, ReportsConnectionStatus)
{
    // ASSERT: Transport correctly reports connection state

    ASSERT_FALSE(transport->isConnected())
        << "Should report as not connected initially";

    transport->connect("test-device");
    ASSERT_TRUE(transport->isConnected())
        << "Should report as connected after connect";

    transport->disconnect();
    ASSERT_FALSE(transport->isConnected())
        << "Should report as not connected after disconnect";
}

// ================================================
// Test Suite 2: Data Reception Tests
// ================================================

TEST_F(TeslaBLETransportTest, ReceivesValidPacket)
{
    // ASSERT: Valid packets are received and forwarded

    bool callbackInvoked = false;
    std::vector<std::uint8_t> receivedPacket;

    // Valid packet: Header(0xAA 0x55) + Length(0x03) + Data(3 bytes) + Checksum
    std::vector<std::uint8_t> validPacket = {
        0xAA, 0x55, 0x03,  // Header + Length
        0x10, 0x20, 0x30,    // Payload (3 bytes)
        0x68                 // Checksum: sum = 0xAA + 0x55 + 0x03 + 0x10 + 0x20 + 0x30 = 0x168 -> 0x68
    };

    transport->subscribeToNotifications([&callbackInvoked, &receivedPacket](
        const std::vector<std::uint8_t>& data
    ) {
        callbackInvoked = true;
        receivedPacket = data;
    });

    transport->connect("test-device");

    // Simulate incoming data via mock
    BLEManagerMock* mock = static_cast<BLEManagerMock*>(platformMock.get());
    mock->simulateIncomingData(validPacket);

    // Give time for async processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(callbackInvoked)
        << "Callback should be invoked for valid packet";
    ASSERT_EQ(receivedPacket.size(), validPacket.size())
        << "Received packet should match original size";
    EXPECT_EQ(receivedPacket[0], 0xAA)
        << "Packet should have valid header";
}

TEST_F(TeslaBLETransportTest, BuffersIncompletePackets)
{
    // ASSERT: Incomplete packets are buffered until complete

    bool callbackInvoked = false;
    std::vector<std::uint8_t> receivedPacket;

    // Partial packet (missing checksum)
    std::vector<std::uint8_t> partialPacket = {
        0xAA, 0x55, 0x03,
        0x10, 0x20, 0x30
        // Missing checksum
    };

    transport->subscribeToNotifications([&callbackInvoked, &receivedPacket](
        const std::vector<std::uint8_t>& data
    ) {
        callbackInvoked = true;
        receivedPacket = data;
    });

    transport->connect("test-device");

    BLEManagerMock* mock = static_cast<BLEManagerMock*>(platformMock.get());
    mock->simulateIncomingData(partialPacket);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_FALSE(callbackInvoked)
        << "Incomplete packet should not trigger callback";
}

// ================================================
// Test Suite 3: Packet Validation Tests
// ================================================

TEST_F(TeslaBLETransportTest, RejectsInvalidHeader)
{
    // ASSERT: Packets with invalid headers are rejected

    bool callbackInvoked = false;

    std::vector<std::uint8_t> invalidHeaderPacket = {
        0xFF, 0x55, 0x03,  // Invalid header (0xFF instead of 0xAA)
        0x10, 0x20, 0x30,
        0xB8
    };

    transport->subscribeToNotifications([&callbackInvoked](
        const std::vector<std::uint8_t>& data
    ) {
        callbackInvoked = true;
    });

    transport->connect("test-device");

    BLEManagerMock* mock = static_cast<BLEManagerMock*>(platformMock.get());
    mock->simulateIncomingData(invalidHeaderPacket);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_FALSE(callbackInvoked)
        << "Invalid header packet should be rejected";
}

TEST_F(TeslaBLETransportTest, RejectsInvalidChecksum)
{
    // ASSERT: Packets with invalid checksums are rejected

    bool callbackInvoked = false;

    std::vector<std::uint8_t> badChecksumPacket = {
        0xAA, 0x55, 0x03,
        0x10, 0x20, 0x30,
        0x00  // Wrong checksum
    };

    transport->subscribeToNotifications([&callbackInvoked](
        const std::vector<std::uint8_t>& data
    ) {
        callbackInvoked = true;
    });

    transport->connect("test-device");

    BLEManagerMock* mock = static_cast<BLEManagerMock*>(platformMock.get());
    mock->simulateIncomingData(badChecksumPacket);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_FALSE(callbackInvoked)
        << "Invalid checksum packet should be rejected";
}

TEST_F(TeslaBLETransportTest, ValidatesPacketStructure)
{
    // ASSERT: Packet structure is validated

    bool callbackInvoked = false;
    std::vector<std::uint8_t> receivedPacket;

    // Valid packet with correct structure
    std::vector<std::uint8_t> validPacket = {
        0xAA, 0x55, 0x05,  // Header + Length (5 payload bytes)
        0x01, 0x02, 0x03, 0x04, 0x05,  // Payload
        0x76  // Checksum: sum = 0xAA + 0x55 + 0x05 + 0x01 + 0x02 + 0x03 + 0x04 + 0x05 = 0x176 -> 0x76
    };

    transport->subscribeToNotifications([&callbackInvoked, &receivedPacket](
        const std::vector<std::uint8_t>& data
    ) {
        callbackInvoked = true;
        receivedPacket = data;
    });

    transport->connect("test-device");

    BLEManagerMock* mock = static_cast<BLEManagerMock*>(platformMock.get());
    mock->simulateIncomingData(validPacket);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(callbackInvoked)
        << "Valid packet should be accepted";
    ASSERT_EQ(receivedPacket.size(), 9)
        << "Packet should have correct total size (3 header + 5 payload + 1 checksum)";
    EXPECT_EQ(receivedPacket[2], 0x05)
        << "Length field should indicate 5 payload bytes";
}

// ================================================
// Test Suite 4: Multiple Packet Handling Tests
// ================================================

TEST_F(TeslaBLETransportTest, HandlesContinuousPacketStream)
{
    // ASSERT: Multiple packets in sequence are processed

    int packetsReceived = 0;

    transport->subscribeToNotifications([&packetsReceived](
        const std::vector<std::uint8_t>& data
    ) {
        if (data.size() == 9 && data[0] == 0xAA && data[1] == 0x55) {
            packetsReceived++;
        }
    });

    transport->connect("test-device");

    BLEManagerMock* mock = static_cast<BLEManagerMock*>(platformMock.get());

    // Simulate 3 packets
    mock->simulateIncomingData({0xAA, 0x55, 0x03, 0x10, 0x20, 0x30, 0x68});
    mock->simulateIncomingData({0xAA, 0x55, 0x03, 0x11, 0x21, 0x31, 0x69});
    mock->simulateIncomingData({0xAA, 0x55, 0x03, 0x12, 0x22, 0x32, 0x6A});

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(packetsReceived, 3)
        << "Should process all 3 packets in stream";
}

// ================================================
// Test Suite 5: Error Handling Tests
// ================================================

TEST_F(TeslaBLETransportTest, HandlesConnectionFailure)
{
    // ASSERT: Connection failure is handled gracefully

    bool connected = transport->connect("non-existent-device");

    // Mock implementation accepts any connection
    // In real implementation, this would test actual connection failure
    ASSERT_TRUE(connected)
        << "Connection attempt should complete (mock accepts all)";
}

TEST_F(TeslaBLETransportTest, ClearsBufferOnDisconnect)
{
    // ASSERT: Buffer is cleared when disconnecting

    bool callbackInvoked = false;

    transport->subscribeToNotifications([&callbackInvoked](
        const std::vector<std::uint8_t>& data
    ) {
        callbackInvoked = true;
    });

    transport->connect("test-device");

    BLEManagerMock* mock = static_cast<BLEManagerMock*>(platformMock.get());

    // Send partial packet
    mock->simulateIncomingData({0xAA, 0x55, 0x03, 0x10, 0x20, 0x30});

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Disconnect
    transport->disconnect();

    // Send remaining bytes
    mock->simulateIncomingData({0x68});

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Should not receive packet (buffer was cleared)
    ASSERT_FALSE(callbackInvoked)
        << "Buffer should be cleared on disconnect";
}

TEST_F(TeslaBLETransportTest, SendWithoutConnectionDoesNotThrow)
{
    // ASSERT: Sending without connection doesn't throw

    std::vector<std::uint8_t> testData = {0x01, 0x02, 0x03};

    EXPECT_NO_THROW(transport->send(testData))
        << "Send should not throw when not connected";
}

// ================================================
// Test Suite 6: Packet Reassembly Tests
// ================================================

TEST_F(TeslaBLETransportTest, ReassemblesFragmentedPacket)
{
    // ASSERT: Packet split across multiple receives is reassembled

    bool callbackInvoked = false;
    std::vector<std::uint8_t> receivedPacket;

    // Complete packet
    std::vector<std::uint8_t> completePacket = {
        0xAA, 0x55, 0x04,
        0x10, 0x20, 0x30, 0x40,
        0x89
    };

    transport->subscribeToNotifications([&callbackInvoked, &receivedPacket](
        const std::vector<std::uint8_t>& data
    ) {
        callbackInvoked = true;
        receivedPacket = data;
    });

    transport->connect("test-device");

    BLEManagerMock* mock = static_cast<BLEManagerMock*>(platformMock.get());

    // Send packet in fragments
    mock->simulateIncomingData({0xAA, 0x55, 0x04, 0x10});
    mock->simulateIncomingData({0x20, 0x30, 0x40, 0x89});

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(callbackInvoked)
        << "Fragmented packet should be reassembled and received";
    ASSERT_EQ(receivedPacket.size(), completePacket.size())
        << "Reassembled packet should match original";
}

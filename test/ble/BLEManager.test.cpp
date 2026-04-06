#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/ble/BLEPlatform.h"

using namespace vehicle_sim;
using testing::_;
using testing::Return;
using testing::SaveArg;
using testing::Eq;
using testing::Field;

// Mock BLE platform for testing
class MockBLEPlatform : public BLEPlatform {
public:
    MOCK_METHOD(std::vector<BLEDeviceInfo>, scanForDevices, (int timeout_seconds), (override));
    MOCK_METHOD(bool, connect, (const std::string& device_identifier), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(void, setDeviceFoundCallback, (DeviceCallback callback), (override));
    MOCK_METHOD(void, setDataReceivedCallback, (DataCallback callback), (override));
    MOCK_METHOD(void, send, (const std::vector<uint8_t>& data), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(std::string, getConnectedDeviceId, (), (const, override));
};

// ================================================
// BLEManager Unit Tests
// TDD - Tests using dependency injection with mocks
// ================================================

TEST(BLEManagerTest, ScansForDevicesViaPlatform)
{
    auto mockPlatform = std::make_unique<MockBLEPlatform>();
    EXPECT_CALL(*mockPlatform, scanForDevices(5))
        .WillOnce(Return(std::vector<BLEDeviceInfo>{
            BLEDeviceInfo{"addr1", "Device 1", false},
            BLEDeviceInfo{"addr2", "Device 2", false}
        }));
    EXPECT_CALL(*mockPlatform, isConnected())
        .WillOnce(Return(false));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    auto devices = manager.scanForDevices(5);
    EXPECT_EQ(devices.size(), 2);
    EXPECT_EQ(devices[0].name, "Device 1");
    EXPECT_EQ(devices[1].name, "Device 2");
}

TEST(BLEManagerTest, ConnectsToDeviceViaPlatform)
{
    auto mockPlatform = std::make_unique<MockBLEPlatform>();
    EXPECT_CALL(*mockPlatform, connect(Eq("test-device")))
        .WillOnce(Return(true));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    bool result = manager.connect("test-device");
    EXPECT_TRUE(result);
}

TEST(BLEManagerTest, ReportsConnectionFailure)
{
    auto mockPlatform = std::make_unique<MockBLEPlatform>();
    EXPECT_CALL(*mockPlatform, connect(Eq("invalid-device")))
        .WillOnce(Return(false));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    bool result = manager.connect("invalid-device");
    EXPECT_FALSE(result);
}

TEST(BLEManagerTest, DisconnectsViaPlatform)
{
    auto mockPlatform = std::make_unique<MockBLEPlatform>();
    EXPECT_CALL(*mockPlatform, disconnect()).Times(1);

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    manager.disconnect();
}

TEST(BLEManagerTest, ReportsConnectionStatus)
{
    auto mockPlatform = std::make_unique<MockBLEPlatform>();
    EXPECT_CALL(*mockPlatform, isConnected())
        .WillOnce(Return(true));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    EXPECT_TRUE(manager.isConnected());
}

TEST(BLEManagerTest, ReturnsConnectedDeviceId)
{
    auto mockPlatform = std::make_unique<MockBLEPlatform>();
    EXPECT_CALL(*mockPlatform, getConnectedDeviceId())
        .WillOnce(Return(std::string("device-123")));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    std::string deviceId = manager.getConnectedDeviceId();
    EXPECT_EQ(deviceId, "device-123");
}

TEST(BLEManagerTest, ReturnsEmptyDeviceIdWhenNotConnected)
{
    auto mockPlatform = std::make_unique<MockBLEPlatform>();
    EXPECT_CALL(*mockPlatform, getConnectedDeviceId())
        .WillOnce(Return(std::string()));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    std::string deviceId = manager.getConnectedDeviceId();
    EXPECT_TRUE(deviceId.empty());
}

TEST(BLEManagerTest, ForwardsDeviceFoundCallback)
{
    auto mockPlatform = std::make_unique<MockBLEPlatform>();
    BLEManager::DeviceCallback capturedCallback;

    EXPECT_CALL(*mockPlatform, setDeviceFoundCallback(_))
        .WillOnce(SaveArg<0>(&capturedCallback));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    bool callbackInvoked = false;
    manager.onDeviceFound([&callbackInvoked](const BLEDeviceInfo& device) {
        callbackInvoked = true;
        EXPECT_EQ(device.name, "Test Device");
    });

    // Simulate the platform invoking the callback
    capturedCallback(BLEDeviceInfo{"addr", "Test Device", false});

    EXPECT_TRUE(callbackInvoked);
}

TEST(BLEManagerTest, ForwardsDataReceivedCallback)
{
    auto mockPlatform = std::make_unique<MockBLEPlatform>();
    BLEManager::DataCallback capturedCallback;

    EXPECT_CALL(*mockPlatform, setDataReceivedCallback(_))
        .WillOnce(SaveArg<0>(&capturedCallback));

    BLEManager manager;
    manager.setPlatform(std::move(mockPlatform));

    bool callbackInvoked = false;
    std::vector<uint8_t> expectedData = {0xAA, 0x55, 0x01, 0x02, 0x03};

    manager.onDataReceived([&callbackInvoked, &expectedData](const std::vector<uint8_t>& data) {
        callbackInvoked = true;
        EXPECT_EQ(data, expectedData);
    });

    // Simulate the platform invoking the callback
    capturedCallback(expectedData);

    EXPECT_TRUE(callbackInvoked);
}

TEST(BLEManagerTest, HandlesNullPlatformGracefully)
{
    BLEManager manager;
    manager.setPlatform(nullptr);

    // Should not crash and return safe defaults
    EXPECT_FALSE(manager.isConnected());
    EXPECT_TRUE(manager.getConnectedDeviceId().empty());
    EXPECT_EQ(manager.scanForDevices(5).size(), 0);
    EXPECT_FALSE(manager.connect("device"));

    // These should not crash either
    manager.disconnect();
    manager.onDeviceFound([](const BLEDeviceInfo&) {});
    manager.onDataReceived([](const std::vector<uint8_t>&) {});
}

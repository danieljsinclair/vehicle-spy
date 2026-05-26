#include <gtest/gtest.h>
#include <memory>
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DBCSignalDefinition.h"

using namespace vehicle_sim::domain;

class DBCTranslationServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        service_ = std::make_unique<DBCTranslationService>();
    }

    std::unique_ptr<DBCTranslationService> service_;
};

TEST_F(DBCTranslationServiceTest, LoadNonexistentVehicle_ReturnsFalse) {
    bool result = service_->loadVehicle("nonexistent_vehicle", VehicleProtocol::CAN);

    EXPECT_FALSE(result);
    EXPECT_FALSE(service_->isLoaded());
}

TEST_F(DBCTranslationServiceTest, ProcessFrameBeforeLoad_ReturnsNullopt) {
    std::vector<std::uint8_t> rawData = {0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x61, 0x00, 0x00};

    auto result = service_->processFrame(rawData);

    EXPECT_FALSE(result.has_value());
}

TEST_F(DBCTranslationServiceTest, IsLoadedBeforeLoad_ReturnsFalse) {
    EXPECT_FALSE(service_->isLoaded());
}

TEST_F(DBCTranslationServiceTest, RegisterAndLoadCANVehicle) {
    VehicleConfig config(
        "test_vehicle.dbc",
        "test_vehicle.dbc",
        "Test Vehicle",
        std::unordered_map<std::string, std::string>{{"DIR_axleSpeed", "motorRpm"}}
    );
    service_->registry().registerVehicle("test_vehicle", std::move(config));

    bool result = service_->loadVehicle("test_vehicle", VehicleProtocol::CAN);

    EXPECT_FALSE(result);  // DBC file doesn't exist, so load fails
    EXPECT_FALSE(service_->isLoaded());
}

TEST_F(DBCTranslationServiceTest, LoadOBD2Vehicle) {
    VehicleConfig config(
        "",  // Empty DBC path for OBD2
        "",  // Empty bundle filename for OBD2
        "OBD2 Vehicle",
        std::unordered_map<std::string, std::string>{}
    );
    service_->registry().registerVehicle("obd2_vehicle", std::move(config));

    bool result = service_->loadVehicle("obd2_vehicle", VehicleProtocol::OBD2);

    EXPECT_TRUE(result);
    EXPECT_TRUE(service_->isLoaded());
}

TEST_F(DBCTranslationServiceTest, ProcessOBD2Frame_ReturnsSignal) {
    VehicleConfig config(
        "",
        "",
        "OBD2 Vehicle",
        std::unordered_map<std::string, std::string>{}
    );
    service_->registry().registerVehicle("obd2_vehicle", std::move(config));

    service_->loadVehicle("obd2_vehicle", VehicleProtocol::OBD2);

    // Standard OBD2 Mode 01 response: [0x41] [PID 0x0D] [speed=100]
    std::vector<std::uint8_t> obd2Frame = {0x41, 0x0D, 100};

    auto result = service_->processFrame(obd2Frame);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->getSpeedKmh().value(), 100.0);
}

TEST_F(DBCTranslationServiceTest, GetProtocol_ReturnsCorrectProtocol) {
    VehicleConfig config("", "", "OBD2 Vehicle", std::unordered_map<std::string, std::string>{});
    service_->registry().registerVehicle("obd2_vehicle", std::move(config));

    service_->loadVehicle("obd2_vehicle", VehicleProtocol::OBD2);

    EXPECT_EQ(service_->getProtocol(), VehicleProtocol::OBD2);
}

TEST_F(DBCTranslationServiceTest, GetVehicleId_ReturnsLoadedId) {
    VehicleConfig config("", "", "OBD2 Vehicle", std::unordered_map<std::string, std::string>{});
    service_->registry().registerVehicle("obd2_vehicle", std::move(config));

    service_->loadVehicle("obd2_vehicle", VehicleProtocol::OBD2);

    EXPECT_EQ(service_->getVehicleId(), "obd2_vehicle");
}

TEST_F(DBCTranslationServiceTest, RegistryReturnsCorrectRegistry) {
    VehicleConfig config("", "", "Test", std::unordered_map<std::string, std::string>{});
    service_->registry().registerVehicle("test", std::move(config));

    EXPECT_TRUE(service_->registry().hasConfig("test"));
}

TEST_F(DBCTranslationServiceTest, ConstRegistryReturnsSameRegistry) {
    VehicleConfig config("", "", "Test", std::unordered_map<std::string, std::string>{});
    service_->registry().registerVehicle("test", std::move(config));

    const auto& constService = *service_;
    EXPECT_TRUE(constService.registry().hasConfig("test"));
}

// --- loadVehicleFromPath tests ---

TEST_F(DBCTranslationServiceTest, LoadFromPath_NonexistentFile_ReturnsFalse) {
    VehicleConfig config(
        "nonexistent.dbc",
        "nonexistent.dbc",
        "Test Vehicle",
        std::unordered_map<std::string, std::string>{{"DI_motorRPM", "motorRpm"}},
        "",
        true
    );
    service_->registry().registerVehicle("test_path", std::move(config));

    bool result = service_->loadVehicleFromPath("test_path", VehicleProtocol::CAN, "/nonexistent/path/to/file.dbc");

    EXPECT_FALSE(result);
    EXPECT_FALSE(service_->isLoaded());
}

TEST_F(DBCTranslationServiceTest, LoadFromPath_NonexistentVehicle_ReturnsFalse) {
    bool result = service_->loadVehicleFromPath("nonexistent", VehicleProtocol::CAN, "resources/dbc/Model3CAN.dbc");

    EXPECT_FALSE(result);
    EXPECT_FALSE(service_->isLoaded());
}

TEST_F(DBCTranslationServiceTest, LoadFromPath_OBD2Vehicle_SucceedsWithoutPath) {
    VehicleConfig config(
        "",
        "",
        "OBD2 Vehicle",
        std::unordered_map<std::string, std::string>{}
    );
    service_->registry().registerVehicle("obd2_path", std::move(config));

    bool result = service_->loadVehicleFromPath("obd2_path", VehicleProtocol::OBD2, "");

    EXPECT_TRUE(result);
    EXPECT_TRUE(service_->isLoaded());
    EXPECT_EQ(service_->getProtocol(), VehicleProtocol::OBD2);
}

TEST_F(DBCTranslationServiceTest, LoadFromPath_ValidTeslaDBCFile_ReturnsTrue) {
    VehicleConfig config(
        "Model3CAN.dbc",
        "Model3CAN.dbc",
        "Tesla Model 3",
        std::unordered_map<std::string, std::string>{{"DI_motorRPM", "motorRpm"}},
        "",
        true
    );
    service_->registry().registerVehicle("tesla_path", std::move(config));

    bool result = service_->loadVehicleFromPath("tesla_path", VehicleProtocol::CAN, "resources/dbc/Model3CAN.dbc");

    EXPECT_TRUE(result);
    EXPECT_TRUE(service_->isLoaded());
    EXPECT_EQ(service_->getProtocol(), VehicleProtocol::CAN);
    EXPECT_EQ(service_->getVehicleId(), "tesla_path");
}

TEST_F(DBCTranslationServiceTest, LoadFromPath_EmptyDBCFile_ReturnsFalse) {
    VehicleConfig config(
        "empty.dbc",
        "empty.dbc",
        "Empty DBC Vehicle",
        std::unordered_map<std::string, std::string>{{"signal", "field"}},
        "",
        true
    );
    service_->registry().registerVehicle("empty_dbc", std::move(config));

    bool result = service_->loadVehicleFromPath("empty_dbc", VehicleProtocol::CAN, "/dev/null");

    EXPECT_FALSE(result);
}

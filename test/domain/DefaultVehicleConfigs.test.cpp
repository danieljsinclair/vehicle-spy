#include <gtest/gtest.h>
#include <memory>
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"

using namespace vehicle_sim::domain;

class DefaultVehicleConfigsTest : public ::testing::Test {
protected:
    void SetUp() override {
        teslaConfig_ = std::make_unique<VehicleConfig>(DefaultVehicleConfigs::teslaModel3());
        audiConfig_ = std::make_unique<VehicleConfig>(DefaultVehicleConfigs::audiMLBEvo());
    }

    std::unique_ptr<VehicleConfig> teslaConfig_;
    std::unique_ptr<VehicleConfig> audiConfig_;
};

TEST_F(DefaultVehicleConfigsTest, TeslaConfig_HasCorrectDBCPath) {
    EXPECT_TRUE(teslaConfig_->dbcFilePath.find("Model3CAN") != std::string::npos);
}

TEST_F(DefaultVehicleConfigsTest, TeslaConfig_HasCorrectVehicleName) {
    EXPECT_EQ(teslaConfig_->vehicleName, "Tesla Model 3");
}

TEST_F(DefaultVehicleConfigsTest, TeslaConfig_HasSignalMappings) {
    EXPECT_GE(teslaConfig_->signalMappings.size(), 4u);
}

TEST_F(DefaultVehicleConfigsTest, TeslaConfig_MapsAxleSpeedToMotorRpm) {
    auto it = teslaConfig_->signalMappings.find("DIR_axleSpeed");
    ASSERT_NE(it, teslaConfig_->signalMappings.end());
    EXPECT_EQ(it->second, "motorRpm");
}

TEST_F(DefaultVehicleConfigsTest, TeslaConfig_MapsTorqueActual) {
    auto it = teslaConfig_->signalMappings.find("DIR_torqueActual");
    ASSERT_NE(it, teslaConfig_->signalMappings.end());
    EXPECT_EQ(it->second, "motorTorqueNm");
}

TEST_F(DefaultVehicleConfigsTest, TeslaConfig_MapsAccelPedalToThrottle) {
    auto it = teslaConfig_->signalMappings.find("DI_accelPedalPos");
    ASSERT_NE(it, teslaConfig_->signalMappings.end());
    EXPECT_EQ(it->second, "throttlePercent");
}

TEST_F(DefaultVehicleConfigsTest, TeslaConfig_MapsSteeringAngle) {
    auto it = teslaConfig_->signalMappings.find("SteeringAngle129");
    ASSERT_NE(it, teslaConfig_->signalMappings.end());
    EXPECT_EQ(it->second, "steeringAngleDeg");
}

TEST_F(DefaultVehicleConfigsTest, AudiConfig_HasCorrectDBCPath) {
    EXPECT_TRUE(audiConfig_->dbcFilePath.find("vw_mlb") != std::string::npos);
}

TEST_F(DefaultVehicleConfigsTest, AudiConfig_HasCorrectVehicleName) {
    EXPECT_EQ(audiConfig_->vehicleName, "Audi MLB Evo");
}

TEST_F(DefaultVehicleConfigsTest, AudiConfig_HasSignalMappings) {
    EXPECT_GE(audiConfig_->signalMappings.size(), 3u);
}

TEST_F(DefaultVehicleConfigsTest, AudiConfig_MapsSpeedSignal) {
    auto it = audiConfig_->signalMappings.find("ESP_v_Signal");
    ASSERT_NE(it, audiConfig_->signalMappings.end());
    EXPECT_EQ(it->second, "speedKmh");
}

TEST_F(DefaultVehicleConfigsTest, AudiConfig_MapsAccelerationSignal) {
    auto it = audiConfig_->signalMappings.find("ESP_Laengsbeschl");
    ASSERT_NE(it, audiConfig_->signalMappings.end());
    EXPECT_EQ(it->second, "accelerationG");
}

TEST_F(DefaultVehicleConfigsTest, AudiConfig_MapsBrakePressure) {
    auto it = audiConfig_->signalMappings.find("ESP_Bremsdruck");
    ASSERT_NE(it, audiConfig_->signalMappings.end());
    EXPECT_EQ(it->second, "brakePercent");
}

TEST_F(DefaultVehicleConfigsTest, AudiConfig_DoesNotHaveTeslaSignals) {
    EXPECT_EQ(audiConfig_->signalMappings.find("DI_accelPedalPos"), audiConfig_->signalMappings.end());
    EXPECT_EQ(audiConfig_->signalMappings.find("SteeringAngle129"), audiConfig_->signalMappings.end());
}

TEST_F(DefaultVehicleConfigsTest, RegisterAll_PopulatesRegistry) {
    VehicleConfigRegistry registry;
    DefaultVehicleConfigs::registerAll(registry);
    EXPECT_TRUE(registry.hasConfig("tesla_model3"));
    EXPECT_TRUE(registry.hasConfig("audi_mlb_evo"));
}

TEST_F(DefaultVehicleConfigsTest, RegisterAll_TeslaConfigRetrievable) {
    VehicleConfigRegistry registry;
    DefaultVehicleConfigs::registerAll(registry);
    const VehicleConfig* config = registry.getConfig("tesla_model3");
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->vehicleName, "Tesla Model 3");
}

TEST_F(DefaultVehicleConfigsTest, RegisterAll_AudiConfigRetrievable) {
    VehicleConfigRegistry registry;
    DefaultVehicleConfigs::registerAll(registry);
    const VehicleConfig* config = registry.getConfig("audi_mlb_evo");
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->vehicleName, "Audi MLB Evo");
}

TEST_F(DefaultVehicleConfigsTest, GearCodeMappings_DefaultIsEmpty) {
    VehicleConfig config("test.dbc", "Test", {}, "", false);
    EXPECT_TRUE(config.gearCodeMappings.empty());
}

TEST_F(DefaultVehicleConfigsTest, GearCodeMappings_CanBeConstructed) {
    std::unordered_map<int, std::string> gearMappings = {
        {0, "P"},
        {1, "R"},
        {2, "N"},
        {3, "D"},
        {4, "S"}
    };
    VehicleConfig config("test.dbc", "Test", {}, "", false, gearMappings);
    EXPECT_EQ(config.gearCodeMappings.size(), 5);
    EXPECT_EQ(config.gearCodeMappings[0], "P");
    EXPECT_EQ(config.gearCodeMappings[1], "R");
    EXPECT_EQ(config.gearCodeMappings[2], "N");
    EXPECT_EQ(config.gearCodeMappings[3], "D");
    EXPECT_EQ(config.gearCodeMappings[4], "S");
}

TEST_F(DefaultVehicleConfigsTest, GearCodeMappings_EqualityIncludesMappings) {
    std::unordered_map<int, std::string> mappings1 = {{0, "P"}, {1, "R"}};
    std::unordered_map<int, std::string> mappings2 = {{0, "P"}, {1, "R"}};
    std::unordered_map<int, std::string> mappings3 = {{0, "P"}, {1, "N"}};

    VehicleConfig config1("test.dbc", "Test", {}, "", false, mappings1);
    VehicleConfig config2("test.dbc", "Test", {}, "", false, mappings2);
    VehicleConfig config3("test.dbc", "Test", {}, "", false, mappings3);

    EXPECT_EQ(config1, config2);
    EXPECT_NE(config1, config3);
}

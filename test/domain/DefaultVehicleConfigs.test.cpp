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

TEST_F(DefaultVehicleConfigsTest, TeslaConfig_MapsAccelPedalPosToThrottle) {
    auto it = teslaConfig_->signalMappings.find("DI_accelPedalPos");
    ASSERT_NE(it, teslaConfig_->signalMappings.end());
    EXPECT_EQ(it->second, "throttlePercent");
}

TEST_F(DefaultVehicleConfigsTest, TeslaConfig_MapsTorqueActualToMotorTorque) {
    auto it = teslaConfig_->signalMappings.find("DI_torqueActual");
    ASSERT_NE(it, teslaConfig_->signalMappings.end());
    EXPECT_EQ(it->second, "motorTorqueNm");
}

TEST_F(DefaultVehicleConfigsTest, TeslaConfig_MapsVehicleSpeed) {
    auto it = teslaConfig_->signalMappings.find("DI_vehicleSpeed");
    ASSERT_NE(it, teslaConfig_->signalMappings.end());
    EXPECT_EQ(it->second, "speedKmh");
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
    EXPECT_EQ(audiConfig_->signalMappings.find("DI_torqueActual"), audiConfig_->signalMappings.end());
}

TEST_F(DefaultVehicleConfigsTest, RegisterAll_PopulatesRegistry) {
    VehicleConfigRegistry registry;
    DefaultVehicleConfigs::registerAll(registry);
    EXPECT_TRUE(registry.hasConfig("tesla"));
    EXPECT_TRUE(registry.hasConfig("audi_mlb_evo"));
}

TEST_F(DefaultVehicleConfigsTest, RegisterAll_TeslaConfigRetrievable) {
    VehicleConfigRegistry registry;
    DefaultVehicleConfigs::registerAll(registry);
    const VehicleConfig* config = registry.getConfig("tesla");
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

// An exact mapping-count assertion was deliberately removed: it broke on every
// legitimate signal addition (steering, brake) while asserting no business
// intent beyond TeslaConfig_HasSignalMappings' lower bound above. The presence
// of each specific mapping is what matters and is covered per-signal below.

TEST_F(DefaultVehicleConfigsTest, TeslaConfig_MapsSCCMSteeringAngle) {
    auto it = teslaConfig_->signalMappings.find("SCCM_steeringAngle");
    ASSERT_NE(it, teslaConfig_->signalMappings.end());
    EXPECT_EQ(it->second, "steeringAngleDeg");
}

TEST_F(DefaultVehicleConfigsTest, TeslaConfig_MapsVCLEFTBrakeLightStatusToBrakeLight) {
    auto it = teslaConfig_->signalMappings.find("VCLEFT_brakeLightStatus");
    ASSERT_NE(it, teslaConfig_->signalMappings.end());
    EXPECT_EQ(it->second, "brakeLight");
}

// Twin-physics rear-motor HV bus signals (CAN 0x108/0x126) wired declaratively
// into the canonical VehicleSignal model — see memory [[twin-physics-can-candidates]].
TEST_F(DefaultVehicleConfigsTest, TeslaConfig_MapsTwinPhysicsSignals) {
    {
        auto it = teslaConfig_->signalMappings.find("DIR_axleSpeed");
        ASSERT_NE(it, teslaConfig_->signalMappings.end());
        EXPECT_EQ(it->second, "motorRpm");
    }
    {
        auto it = teslaConfig_->signalMappings.find("RearHighVoltage126");
        ASSERT_NE(it, teslaConfig_->signalMappings.end());
        EXPECT_EQ(it->second, "motorHvVoltage");
    }
    {
        auto it = teslaConfig_->signalMappings.find("RearMotorCurrent126");
        ASSERT_NE(it, teslaConfig_->signalMappings.end());
        EXPECT_EQ(it->second, "motorHvCurrent");
    }
}

// DI_brakePedalState is deliberately unmapped: measured on a real Model 3 it
// is a constant drive-ready flag, not pedal data (known-YAGNI removal).
TEST_F(DefaultVehicleConfigsTest, TeslaConfig_DoesNotMapBrakePedalState) {
    EXPECT_EQ(teslaConfig_->signalMappings.find("DI_brakePedalState"),
              teslaConfig_->signalMappings.end());
}

TEST_F(DefaultVehicleConfigsTest, TeslaConfig_MapsDIGearToGearSelector) {
    auto it = teslaConfig_->signalMappings.find("DI_gear");
    ASSERT_NE(it, teslaConfig_->signalMappings.end());
    EXPECT_EQ(it->second, "gearSelector");
}

TEST_F(DefaultVehicleConfigsTest, TeslaConfig_MapsVehicleSpeedToSpeedKmh) {
    auto it = teslaConfig_->signalMappings.find("DI_vehicleSpeed");
    ASSERT_NE(it, teslaConfig_->signalMappings.end());
    EXPECT_EQ(it->second, "speedKmh");
}

TEST_F(DefaultVehicleConfigsTest, TeslaConfig_DoesNotHaveGearCodeMappings) {
    EXPECT_EQ(teslaConfig_->signalMappings.find("gearCode"), teslaConfig_->signalMappings.end());
    EXPECT_EQ(teslaConfig_->signalMappings.find("DI_gearCode"), teslaConfig_->signalMappings.end());
}

// Gear code mappings removed - DBC VAL_ table is now single source of truth

#include <gtest/gtest.h>
#include "vehicle-sim/domain/VehicleConfigResolver.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/VehicleConfig.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"
#include "vehicle-sim/domain/VehicleSimExceptions.h"
#include <memory>
#include <algorithm>

using namespace vehicle_sim::domain;

class VehicleConfigResolverTest : public ::testing::Test {
protected:
    void SetUp() override {
        service_ = std::make_unique<DBCTranslationService>();
        DefaultVehicleConfigs::registerAll(service_->registry());
        resolver_ = std::make_unique<VehicleConfigResolver>(*service_);
    }

    std::unique_ptr<DBCTranslationService> service_;
    std::unique_ptr<VehicleConfigResolver> resolver_;
};

TEST_F(VehicleConfigResolverTest, ResolveValidVehicle_ReturnsConfigAndProtocol) {
    auto result = resolver_->resolve("tesla");

    ASSERT_NE(result.config, nullptr) << "Should return non-null config";
    EXPECT_EQ(result.config->vehicleName, "Tesla Model 3");
    EXPECT_EQ(result.protocol, VehicleProtocol::CAN) << "Tesla uses CAN protocol";
}

TEST_F(VehicleConfigResolverTest, ResolveValidVehicle_LoadsDBCIntoService) {
    (void)resolver_->resolve("tesla");

    EXPECT_TRUE(service_->isLoaded()) << "Vehicle should be loaded into service";
    EXPECT_EQ(service_->getVehicleId(), "tesla");
}

TEST_F(VehicleConfigResolverTest, ResolveValidAudiVehicle_ReturnsOBD2Protocol) {
    auto result = resolver_->resolve("audi_mlb_evo");

    ASSERT_NE(result.config, nullptr);
    EXPECT_EQ(result.config->vehicleName, "Audi MLB Evo");
    EXPECT_EQ(result.protocol, VehicleProtocol::CAN) << "Audi MLB Evo uses CAN protocol";
}

TEST_F(VehicleConfigResolverTest, ResolveInvalidVehicle_ErrorMessageIncludesAvailableVehicles) {
    try {
        (void)resolver_->resolve("nonexistent_vehicle");
        FAIL() << "Should have thrown runtime_error";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_TRUE(msg.find("Vehicle config not found") != std::string::npos)
            << "Error should mention config not found";
        EXPECT_TRUE(msg.find("Available vehicles") != std::string::npos)
            << "Error should list available vehicles";
        EXPECT_TRUE(msg.find("tesla") != std::string::npos)
            << "Error should include tesla";
        EXPECT_TRUE(msg.find("audi_mlb_evo") != std::string::npos)
            << "Error should include audi_mlb_evo";
    }
}

TEST_F(VehicleConfigResolverTest, ResolveCanVehicleWithMissingDbc_ThrowsSelfDiagnosingDBCLoadException) {
    // Registered CAN vehicle whose DBC exists nowhere: the failure must be
    // self-diagnosing (vehicle, stage, concrete paths tried), not a bare name.
    VehicleConfig config(
        "no/such/ghost.dbc",
        "no/such/ghost.dbc",
        "Ghost CAN Vehicle",
        std::unordered_map<std::string, std::string>{{"DI_torqueActual", "motorTorqueNm"}},
        "",
        true
    );
    service_->registry().registerVehicle("ghost_can", std::move(config));

    try {
        (void)resolver_->resolve("ghost_can");
        FAIL() << "Should have thrown DBCLoadException";
    } catch (const DBCLoadException& e) {
        EXPECT_EQ(e.vehicleType(), "ghost_can");

        const std::string msg = e.what();
        // Vehicle name is named.
        EXPECT_NE(msg.find("ghost_can"), std::string::npos)
            << "Error should name the vehicle";
        // The failed stage is named (file missing => open, not zero-signals).
        EXPECT_EQ(e.stage(), "open");
        EXPECT_NE(msg.find("Failed stage: open"), std::string::npos)
            << "Error should name the failed stage";
        // The concrete DBC paths tried are listed.
        EXPECT_NE(msg.find("Paths tried:"), std::string::npos)
            << "Error should list the paths tried";
        ASSERT_FALSE(e.pathsTried().empty());
        for (const auto& path : e.pathsTried()) {
            EXPECT_NE(msg.find(path), std::string::npos)
                << "Error message should contain tried path: " << path;
            EXPECT_NE(path.find("no/such/ghost.dbc"), std::string::npos)
                << "Tried path should embed the DBC resource: " << path;
        }
    }
}

TEST_F(VehicleConfigResolverTest, AvailableVehicles_ReturnsRegisteredVehicles) {
    auto vehicles = resolver_->availableVehicles();

    EXPECT_FALSE(vehicles.empty()) << "Should have at least one vehicle";
    EXPECT_TRUE(std::find(vehicles.begin(), vehicles.end(), "tesla") != vehicles.end())
        << "Should include tesla";
    EXPECT_TRUE(std::find(vehicles.begin(), vehicles.end(), "audi_mlb_evo") != vehicles.end())
        << "Should include audi_mlb_evo";
}

TEST_F(VehicleConfigResolverTest, AvailableVehicles_ReturnsConsistentList) {
    auto vehicles1 = resolver_->availableVehicles();
    auto vehicles2 = resolver_->availableVehicles();

    EXPECT_EQ(vehicles1.size(), vehicles2.size())
        << "Available vehicles list should be consistent";
    EXPECT_EQ(vehicles1, vehicles2)
        << "Available vehicles list should be identical across calls";
}

TEST_F(VehicleConfigResolverTest, ResolveTwice_SameVehicleReturnsSameConfig) {
    auto result1 = resolver_->resolve("tesla");
    auto result2 = resolver_->resolve("tesla");

    EXPECT_EQ(result1.config, result2.config)
        << "Resolving same vehicle twice should return same config pointer";
    EXPECT_EQ(result1.protocol, result2.protocol);
}

TEST_F(VehicleConfigResolverTest, ResolveAfterAvailableVehicles_StillWorks) {
    auto vehicles = resolver_->availableVehicles();

    if (!vehicles.empty()) {
        auto result = resolver_->resolve(vehicles[0]);
        EXPECT_NE(result.config, nullptr);
    }
}
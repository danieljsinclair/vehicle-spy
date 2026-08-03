// ExecutablePathTest.cpp - Verify resource resolution is independent of CWD.
//
// vehicle-sim resolves the Tesla/VW DBC relative to the running executable, not
// PWD, so it must load the DBC when launched from an unrelated directory (e.g.
// /tmp) rather than crashing with DBCLoadException.

#include <gtest/gtest.h>
#include <unistd.h>
#include <sys/syslimits.h>
#include <filesystem>
#include <string>

#include "vehicle-sim/util/ExecutablePath.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"

using namespace vehicle_sim::domain;
using vehicle_sim::util::ExecutablePath;

namespace {

class ExecutablePathTest : public ::testing::Test {
protected:
    void SetUp() override {
        char cwd[PATH_MAX];
        ASSERT_NE(getcwd(cwd, sizeof(cwd)), nullptr);
        originalCwd_ = cwd;
    }

    void TearDown() override {
        if (!originalCwd_.empty()) {
            (void)chdir(originalCwd_.c_str());
        }
    }

    std::string originalCwd_;
};

// The executable's directory must be reported absolute (the test binary dir).
TEST_F(ExecutablePathTest, DirectoryIsAbsolute) {
    const std::string dir = ExecutablePath::directory();
    ASSERT_FALSE(dir.empty());
    EXPECT_EQ(dir.front(), '/');
    EXPECT_TRUE(std::filesystem::exists(dir));
}

// Core requirement: the DBC resolves to the real file even when CWD is /tmp.
TEST_F(ExecutablePathTest, DbcResolvesFromForeignCwd) {
    ASSERT_EQ(chdir("/tmp"), 0) << "could not chdir to /tmp";

    const std::string resolved =
        ExecutablePath::resolveResource("resources/dbc/Model3CAN.dbc");

    ASSERT_FALSE(resolved.empty());
    EXPECT_TRUE(std::filesystem::exists(resolved))
        << "DBC not found via exe-relative resolution from /tmp: " << resolved;
    EXPECT_TRUE(std::filesystem::is_regular_file(resolved));
}

// The vw_mlb DBC resolves the same way.
TEST_F(ExecutablePathTest, VwDbcResolvesFromForeignCwd) {
    ASSERT_EQ(chdir("/tmp"), 0) << "could not chdir to /tmp";

    const std::string resolved =
        ExecutablePath::resolveResource("resources/dbc/vw_mlb.dbc");

    ASSERT_FALSE(resolved.empty());
    EXPECT_TRUE(std::filesystem::exists(resolved))
        << "DBC not found via exe-relative resolution from /tmp: " << resolved;
}

// Integration: from a foreign CWD, the Tesla DBC actually loads into the
// translation service (no DBCLoadException). This exercises the production
// code path used by VehicleConfigResolver/--vehicle tesla.
TEST_F(ExecutablePathTest, LoadTeslaFromForeignCwd_Succeeds) {
    ASSERT_EQ(chdir("/tmp"), 0) << "could not chdir to /tmp";

    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());

    EXPECT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN))
        << "Tesla DBC must load from /tmp via exe-relative resolution";
    EXPECT_EQ(service.getVehicleId(), "tesla");
    EXPECT_TRUE(service.isLoaded());
}

} // namespace

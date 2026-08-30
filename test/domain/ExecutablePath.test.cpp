// ExecutablePathTest.cpp - Verify resource resolution is independent of CWD.
//
// vehicle-sim resolves the Tesla/VW DBC relative to the running executable, not
// PWD, so it must load the DBC when launched from an unrelated directory (e.g.
// /tmp) rather than crashing with DBCLoadException.

#include <gtest/gtest.h>
#include <unistd.h>
#include <sys/syslimits.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

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

// The candidate list resolveResource() walks must be reported so load-failure
// diagnostics can show every concrete path tried. Order: exe dir first, then
// ancestors, with the CWD fallback present (deduped when CWD is itself an
// exe ancestor, e.g. running the tests from the repo root).
TEST_F(ExecutablePathTest, ResourceCandidates_ExeDirFirst_CwdPresent) {
    const std::string rel = "resources/dbc/Model3CAN.dbc";
    const std::vector<std::string> candidates = ExecutablePath::resourceCandidates(rel);

    ASSERT_GE(candidates.size(), 2u) << "expected exe-dir and cwd candidates";

    // First candidate is anchored at the executable's directory.
    EXPECT_EQ(candidates.front(), ExecutablePath::directory() + "/" + rel)
        << "first candidate must be <exeDir>/<resource>";

    // The CWD fallback candidate is in the list.
    char cwd[PATH_MAX];
    ASSERT_NE(getcwd(cwd, sizeof(cwd)), nullptr);
    const std::string cwdCandidate = std::string(cwd) + "/" + rel;
    EXPECT_NE(std::find(candidates.begin(), candidates.end(), cwdCandidate), candidates.end())
        << "CWD fallback candidate must be reported";

    // Every candidate embeds the requested resource path.
    for (const auto& candidate : candidates) {
        EXPECT_NE(candidate.find(rel), std::string::npos)
            << "candidate missing the resource path: " << candidate;
    }
}

// A candidate that exists is exactly what resolveResource() returns, and the
// real repo DBC is among the candidates for the test binary (exe-ancestor walk).
TEST_F(ExecutablePathTest, ResourceCandidates_ContainsExistingResourceResolveReturns) {
    const std::string rel = "resources/dbc/Model3CAN.dbc";
    const std::vector<std::string> candidates = ExecutablePath::resourceCandidates(rel);

    const std::string resolved = ExecutablePath::resolveResource(rel);
    ASSERT_TRUE(std::filesystem::exists(resolved));
    EXPECT_NE(std::find(candidates.begin(), candidates.end(), resolved), candidates.end())
        << "resolveResource() result must be one of the reported candidates";
}

// A resource that exists nowhere still yields a non-empty candidate list (the
// walk itself must not depend on the file existing) — this is what a
// DBCLoadException "paths tried" section is built from.
TEST_F(ExecutablePathTest, ResourceCandidates_MissingResourceStillListsCandidates) {
    const std::string rel = "no/such/resource.dbc";
    const std::vector<std::string> candidates = ExecutablePath::resourceCandidates(rel);

    EXPECT_FALSE(candidates.empty());
    for (const auto& candidate : candidates) {
        EXPECT_FALSE(std::filesystem::exists(candidate))
            << "expected no candidate to exist: " << candidate;
    }
}

} // namespace

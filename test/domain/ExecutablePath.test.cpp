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
#include <fstream>
#include <string>
#include <unordered_map>
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

// ---- Absolute-path handling + walk-depth coverage ------------------------
//
// Regression (deep-checkout failure): resolveResource() had no absolute-path
// short-circuit and the upward walk was capped at 8 levels. An absolute input
// was joined under every exe-dir ancestor ("<dir>/abs") and was only found by
// accident when the walk reached "/" and emitted "//abs", which POSIX
// collapses. From a checkout deeper than 8 levels the cap bit before "/" and
// an EXISTING absolute path resolved to non-existent garbage — wholesale DBC
// load failures, because loadVehicle() -> DBCFileParser::parseFile()
// double-resolves (the first resolve returns an absolute path, parseFile
// re-resolves it).

// RAII temp dir (mkdtemp) so absolute-path tests never depend on repo layout.
class TempDir {
public:
    TempDir() {
        const std::filesystem::path base = std::filesystem::temp_directory_path();
        std::string tmpl = (base / "vsim-execpath-XXXXXX").string();
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        if (const char* made = ::mkdtemp(buf.data())) path_ = made;
    }
    ~TempDir() {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const std::string& path() const { return path_; }

private:
    std::string path_;
};

// One-signal DBC in a temp dir; returns its ABSOLUTE path.
std::string writeTempDbc(const TempDir& dir) {
    const std::string abs = dir.path() + "/absolute.dbc";
    std::ofstream out(abs);
    out << "BO_ 264 DI_torque: 8 PARTY\n"
           " SG_ DI_torqueActual : 27|13@1- (2,0) [-7500|7500] \"Nm\" PARTY\n";
    return abs;
}

// An absolute path that exists must resolve to exactly that path, at ANY
// executable depth (pre-short-circuit: "//abs" from the root join, or
// non-existent "<exeDir>/abs" garbage from deep checkouts).
TEST_F(ExecutablePathTest, ResolveResource_AbsoluteExistingPath_ReturnsItExactly) {
    const TempDir dir;
    ASSERT_FALSE(dir.path().empty());
    const std::string abs = writeTempDbc(dir);
    ASSERT_TRUE(std::filesystem::exists(abs));

    EXPECT_EQ(ExecutablePath::resolveResource(abs), abs);
}

// Best-effort contract for a missing absolute path: the input itself, not an
// exe/CWD-anchored composition of it.
TEST_F(ExecutablePathTest, ResolveResource_AbsoluteMissingPath_ReturnsItExactly) {
    const std::string abs = "/no/such/dir/absolute.dbc";
    EXPECT_EQ(ExecutablePath::resolveResource(abs), abs);
}

// Anchoring an absolute path under the exe dir or CWD produces garbage
// candidates ("<dir>//abs"); the candidate list for an absolute input is the
// input alone (it is what load-failure diagnostics must report).
TEST_F(ExecutablePathTest, ResourceCandidates_AbsolutePath_IsThePathAlone) {
    const std::string abs = "/opt/vehicle-sim/resources/dbc/Model3CAN.dbc";
    const std::vector<std::string> candidates = ExecutablePath::resourceCandidates(abs);
    const std::vector<std::string> expected{abs};
    EXPECT_EQ(candidates, expected);
}

// The exe-dir walk must reach the filesystem root regardless of checkout
// depth: the exact invariant the old 8-level cap violated.
TEST_F(ExecutablePathTest, ResourceCandidates_WalkReachesFilesystemRoot) {
    const std::string rel = "resources/dbc/Model3CAN.dbc";
    const std::vector<std::string> candidates = ExecutablePath::resourceCandidates(rel);
    EXPECT_NE(std::find(candidates.begin(), candidates.end(), "/" + rel), candidates.end())
        << "candidate list must include the filesystem-root anchored path";
}

// Production chain: loadVehicleFromPath hands the absolute path to parseFile,
// which re-resolves it — the double-resolve that failed from deep checkouts.
// Loading from an absolute temp DBC must succeed.
TEST_F(ExecutablePathTest, LoadVehicleFromPath_AbsoluteTempDbc_Loads) {
    const TempDir dir;
    ASSERT_FALSE(dir.path().empty());
    const std::string abs = writeTempDbc(dir);

    DBCTranslationService service;
    service.registry().registerVehicle("absolute_tmp", VehicleConfig(
        abs, abs, "Absolute Temp Vehicle",
        std::unordered_map<std::string, std::string>{{"DI_torqueActual", "torqueNm"}},
        "Nm", true));

    EXPECT_TRUE(service.loadVehicleFromPath("absolute_tmp", VehicleProtocol::CAN, abs));
    EXPECT_TRUE(service.isLoaded());
}

} // namespace

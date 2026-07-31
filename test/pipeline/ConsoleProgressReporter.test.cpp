#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/ConsoleProgressReporter.h"
#include "vehicle-sim/pipeline/PipelineReplay.h"
#include "vehicle-sim/pipeline/IProgressReporter.h"
#include "vehicle-sim/pipeline/FileTransport.h"
#include "vehicle-sim/pipeline/CaptureNormaliser.h"
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"
#include "vehicle-sim/domain/VehicleSignal.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::domain;

namespace {

/// Records every call so tests can assert cadence and completeness without
/// touching real stdout. (A fake, not a mock — keeps the test decoupled from a
/// mocking framework and focuses on observable behaviour, not call text.)
class RecordingReporter final : public IProgressReporter {
public:
    std::size_t frameCalls = 0;
    std::size_t completeCalls = 0;
    std::size_t lastFrameIndex = 0;
    std::optional<std::uint64_t> lastTimestamp;
    ReplayStats lastStats{};

    void onFrame(const VehicleSignal& signal, std::size_t frameIndex,
                 std::size_t /*totalHints*/) noexcept override {
        ++frameCalls;
        lastFrameIndex = frameIndex;
        lastTimestamp = signal.getTimestampUtcMs();
    }
    void onComplete(const ReplayStats& stats) noexcept override {
        ++completeCalls;
        lastStats = stats;
    }
};

class TempDir {
public:
    TempDir()
        : path_(std::filesystem::temp_directory_path() /
                ("vhsim_prog_" + std::to_string(counter_++))) {
        std::filesystem::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path_, ec); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    [[nodiscard]] std::string writeCapture(const std::string& name,
                                           const std::string& content) const {
        auto p = (path_ / name).string();
        std::ofstream out(p); out << content;
        return p;
    }
    [[nodiscard]] std::string base(const std::string& name) const {
        return (path_ / name).string();
    }
private:
    std::filesystem::path path_;
    static int counter_;
};
int TempDir::counter_ = 0;

/// Minimal end-to-end harness: file with a couple of decodable Tesla frames.
struct ReplayHarness {
    TempDir dir;
    DBCTranslationService service;
    std::string capture;
    ReplayHarness() {
        DefaultVehicleConfigs::registerAll(service.registry());
        service.loadVehicle("tesla", VehicleProtocol::CAN);
        // Two real-shaped Tesla CAN frames (legacy CSV form).
        capture = dir.writeCapture("cap.csv",
            "timestamp_ms,can_id,dlc,data_hex\n"
            "1000,118,8,3C00180004A001FF\n"
            "2000,118,8,3C00180004A001FF\n"
        );
    }
    [[nodiscard]] std::string base(const std::string& name) const {
        return dir.base(name);
    }
};

} // namespace

// ============================================================
// IProgressReporter seam: runReplay must invoke the reporter once per decoded
// frame, then once for completion — independent of transport (file here).
// ============================================================

TEST(PipelineReplayProgressTest, ReporterReceivesOneFrameCallPerDecodedFrameAndOneComplete) {
    ReplayHarness h;
    FileTransport transport(h.capture);
    ASSERT_TRUE(transport.open());
    CaptureNormaliser normaliser;
    DecodedCsvSink sink(h.base("out"));
    ASSERT_TRUE(sink.isValid());

    RecordingReporter reporter;
    auto stats = runReplay(transport, normaliser, h.service, &sink, /*rawSink=*/nullptr, &reporter);

    // At least the two frames decoded produced two onFrame calls; onComplete exactly once.
    EXPECT_GE(reporter.frameCalls, 2u);
    EXPECT_EQ(reporter.completeCalls, 1u);
    // The frame index passed is 0-based and monotonic.
    EXPECT_EQ(reporter.lastFrameIndex, stats.framesDecoded - 1);
    EXPECT_EQ(reporter.lastStats.framesDecoded, stats.framesDecoded);
}

TEST(PipelineReplayProgressTest, NullReporterRunsSilentlyWithoutCrashing) {
    // Default argument / nullptr must be tolerated (Phase 1 silence contract).
    ReplayHarness h;
    FileTransport transport(h.capture);
    ASSERT_TRUE(transport.open());
    CaptureNormaliser normaliser;
    DecodedCsvSink sink(h.base("out2"));
    ASSERT_TRUE(sink.isValid());

    auto stats = runReplay(transport, normaliser, h.service, &sink, /*rawSink=*/nullptr);
    EXPECT_GE(stats.framesDecoded, 1u);
}

// ============================================================
// ConsoleProgressReporter — throttling and output shape.
// ============================================================

TEST(ConsoleProgressReporterTest, EmitsEveryFrameOnNewline) {
    std::ostringstream out;
    ConsoleProgressReporter reporter(out, "tesla");
    VehicleSignal first(VehicleSignal::Params{.timestampUtcMs = 1000ULL, .throttlePercent = 50.0, .speedKmh = 80.0, .accelerationG = {}, .brakePercent = {}, .steeringAngleDeg = {}, .motorRpm = {}, .motorHvVoltage = {}, .motorHvCurrent = {}, .motorTorqueNm = {}, .gearSelector = 4097});
    VehicleSignal second(VehicleSignal::Params{.timestampUtcMs = 1500ULL, .throttlePercent = 51.0, .speedKmh = 81.0, .accelerationG = {}, .brakePercent = {}, .steeringAngleDeg = {}, .motorRpm = {}, .motorHvVoltage = {}, .motorHvCurrent = {}, .motorTorqueNm = {}, .gearSelector = 4097});

    reporter.onFrame(first, 0, 0);
    reporter.onFrame(second, 1, 0);
    reporter.onComplete(ReplayStats{});

    const auto s = out.str();
    const auto newlineCount = static_cast<std::size_t>(std::count(s.begin(), s.end(), '\n'));
    EXPECT_EQ(newlineCount, 2u);
    EXPECT_NE(s.find("vehicle_id=tesla"), std::string::npos);
    EXPECT_NE(s.find("timestamp_ms=1000"), std::string::npos);
    EXPECT_NE(s.find("timestamp_ms=1500"), std::string::npos);
}

TEST(ConsoleProgressReporterTest, EmitsAllSchemaFields) {
    std::ostringstream out;
    ConsoleProgressReporter reporter(out, "tesla");
    VehicleSignal sig(VehicleSignal::Params{.timestampUtcMs = 1234567ULL, .throttlePercent = 42.5, .speedKmh = 88.0, .accelerationG = 0.25, .brakePercent = 7.5, .steeringAngleDeg = -13.0, .motorRpm = 1234.0, .motorHvVoltage = 390.0, .motorHvCurrent = 120.0, .motorTorqueNm = 250.0, .gearSelector = 4097});
    reporter.onFrame(sig, 0, 0);
    reporter.onComplete(ReplayStats{});

    const auto s = out.str();
    EXPECT_NE(s.find("frame 1"), std::string::npos);
    EXPECT_NE(s.find("timestamp_ms=1234567"), std::string::npos);
    EXPECT_NE(s.find("vehicle_id=tesla"), std::string::npos);
    EXPECT_NE(s.find("speed_kmh=88.00"), std::string::npos);
    EXPECT_NE(s.find("throttle_percent=42.50"), std::string::npos);
    EXPECT_NE(s.find("brake_percent=7.50"), std::string::npos);
    EXPECT_NE(s.find("acceleration_g=0.25"), std::string::npos);
    EXPECT_NE(s.find("steering_angle_deg=-13.00"), std::string::npos);
    EXPECT_NE(s.find("motor_rpm=1234.00"), std::string::npos);
    EXPECT_NE(s.find("motor_hv_voltage=390.00"), std::string::npos);
    EXPECT_NE(s.find("motor_hv_current=120.00"), std::string::npos);
    EXPECT_NE(s.find("motor_torque_nm=250.00"), std::string::npos);
    EXPECT_NE(s.find("gear_selector=D"), std::string::npos);
}

TEST(ConsoleProgressReporterTest, ShowsPercentageWhenTotalHintsKnown) {
    std::ostringstream out;
    ConsoleProgressReporter reporter(out, "tesla");
    VehicleSignal sig(VehicleSignal::Params{.timestampUtcMs = 100ULL, .throttlePercent = 1.0, .speedKmh = 2.0, .accelerationG = {}, .brakePercent = {}, .steeringAngleDeg = {}, .motorRpm = {}, .motorHvVoltage = {}, .motorHvCurrent = {}, .motorTorqueNm = {}, .gearSelector = 4097});
    reporter.onFrame(sig, 49, /*totalHints=*/100); // 50%
    reporter.onComplete(ReplayStats{});

    const auto s = out.str();
    EXPECT_NE(s.find("frame 50 (50.0%)"), std::string::npos);
}

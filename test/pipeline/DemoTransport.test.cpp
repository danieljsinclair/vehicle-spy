#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/DemoTransport.h"
#include "vehicle-sim/pipeline/LiveTwaiSource.h"
#include "vehicle-sim/pipeline/PipelineReplay.h"
#include "vehicle-sim/pipeline/DecodedCsvSink.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::domain;

// ============================================================
// DemoTransport — a synthetic LIVE transport that emits raw-adapter text
// lines (NOT VehicleSignals) through the canonical seam. The lines must:
//   1. follow the "<ID> <D0> ... <D7>" form (LiveTwaiSource's inline parser
//      decodes them),
//   2. decode through a Tesla DBC into real VehicleSignals (proves the
//      frames are plausible, not garbage),
//   3. terminate (bounded) so the replay loop ends.
// Demo no longer knows about VehicleSignal — it only knows the byte layout
// of the demo frames, which is data.
// ============================================================

TEST(DemoTransportTest, OpenSucceedsAndIsOpenUntilExhausted) {
    DemoTransport t(3);
    ASSERT_TRUE(t.open());
    EXPECT_TRUE(t.isOpen());
    ASSERT_TRUE(t.nextLine().has_value());
    ASSERT_TRUE(t.nextLine().has_value());
    ASSERT_TRUE(t.nextLine().has_value());
    EXPECT_FALSE(t.nextLine().has_value());  // EOF
    EXPECT_FALSE(t.isOpen());
}

TEST(DemoTransportTest, OpenIsIdempotent) {
    DemoTransport t(1);
    ASSERT_TRUE(t.open());
    EXPECT_TRUE(t.open());
}

TEST(DemoTransportTest, NextLineBeforeOpenReturnsNullopt) {
    DemoTransport t(1);
    EXPECT_FALSE(t.nextLine().has_value());
}

TEST(DemoTransportTest, LinesAreRawAdapterForm_ParseThroughLiveTwaiSource) {
    DemoTransport t(6);
    ASSERT_TRUE(t.open());
    LiveTwaiSource src(t);
    ASSERT_TRUE(src.open());

    std::set<std::uint32_t> canIds;
    int framesParsed = 0;
    while (auto f = src.nextFrame()) {
        canIds.insert(static_cast<std::uint32_t>(f->bytes[0])
                      | (static_cast<std::uint32_t>(f->bytes[1]) << 8));
        ++framesParsed;
    }
    EXPECT_EQ(framesParsed, 6);
    // The demo emits three distinct Tesla CAN IDs: 264, 280, 599.
    EXPECT_EQ(canIds.count(264), 1u);
    EXPECT_EQ(canIds.count(280), 1u);
    EXPECT_EQ(canIds.count(599), 1u);
}

TEST(DemoTransportTest, FramesDecodeThroughTeslaDbcIntoVehicleSignals) {
    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    DemoTransport transport(60);
    ASSERT_TRUE(transport.open());
    LiveTwaiSource src(transport);
    ASSERT_TRUE(src.open());

    bool sawSpeed = false;
    bool sawThrottle = false;
    bool sawTorque = false;
    bool sawGear = false;
    int decoded = 0;

    auto stats = runReplay(src, service, ReplayOutputs{});
    (void)stats;

    // Re-run to inspect the decoded signals (the previous run consumed the
    // transport). LiveTwaiSource stamps wall-clock on each frame.
    DemoTransport t2(60);
    ASSERT_TRUE(t2.open());
    LiveTwaiSource src2(t2);
    ASSERT_TRUE(src2.open());
    while (auto f = src2.nextFrame()) {
        std::vector<std::uint8_t> bytes(f->bytes.begin(), f->bytes.end());
        auto sig = service.processFrame(bytes, f->timestampMs);
        if (sig) {
            ++decoded;
            if (sig->getSpeedKmh().has_value()) sawSpeed = true;
            if (sig->getThrottlePercent().has_value()) sawThrottle = true;
            if (sig->getMotorTorqueNm().has_value()) sawTorque = true;
            if (sig->getGearSelector().has_value()) sawGear = true;

            auto nowMs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            EXPECT_GT(sig->getTimestampUtcMs(), 0u)
                << "live signal must not have timestamp 0";
            EXPECT_GE(sig->getTimestampUtcMs(), nowMs - 60000u)
                << "live signal timestamp should be recent";
            EXPECT_LE(sig->getTimestampUtcMs(), nowMs + 60000u)
                << "live signal timestamp should be recent";
        }
    }
    EXPECT_GT(decoded, 0) << "demo frames should decode under Tesla DBC";
    EXPECT_TRUE(sawSpeed);
    EXPECT_TRUE(sawThrottle);
    EXPECT_TRUE(sawTorque);
    EXPECT_TRUE(sawGear);
}

TEST(DemoTransportTest, SpeedRampsAcrossDrivingLoop) {
    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    DemoTransport t(120);
    ASSERT_TRUE(t.open());
    LiveTwaiSource src(t);
    ASSERT_TRUE(src.open());

    double peakSpeed = 0.0;
    while (auto f = src.nextFrame()) {
        const auto canId = static_cast<std::uint32_t>(f->bytes[0])
                         | (static_cast<std::uint32_t>(f->bytes[1]) << 8);
        if (canId != 599) continue;
        std::vector<std::uint8_t> bytes(f->bytes.begin(), f->bytes.end());
        auto sig = service.processFrame(bytes, f->timestampMs);
        if (sig && sig->getSpeedKmh().has_value()) {
            peakSpeed = std::max(peakSpeed, *sig->getSpeedKmh());
        }
    }
    EXPECT_GT(peakSpeed, 50.0) << "demo speed should ramp well above 50 kph";
}

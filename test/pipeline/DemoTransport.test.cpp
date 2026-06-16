#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/DemoTransport.h"
#include "vehicle-sim/pipeline/RawFrameNormaliser.h"
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
//   1. follow the "<ID> <D0> ... <D7>" form (RawFrameNormaliser parses them),
//   2. decode through a Tesla DBC into real VehicleSignals (proves the frames
//      are plausible, not garbage),
//   3. terminate (bounded) so the replay loop ends.
// Demo no longer knows about VehicleSignal — it only knows the byte layout of
// the demo frames, which is data.
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

TEST(DemoTransportTest, LinesAreRawAdapterForm_ParseThroughRawFrameNormaliser) {
    DemoTransport t(6);
    ASSERT_TRUE(t.open());
    RawFrameNormaliser n;

    std::set<std::uint32_t> canIds;
    int framesParsed = 0;
    while (auto line = t.nextLine()) {
        auto r = n.normalise(*line);
        ASSERT_EQ(r.kind, NormaliserResultKind::Frame)
            << "demo line did not parse as a frame: " << *line;
        canIds.insert(r.frame.canId);
        ++framesParsed;
    }
    EXPECT_EQ(framesParsed, 6);
    // The demo emits three distinct Tesla CAN IDs: 264, 280, 599.
    EXPECT_EQ(canIds.count(264), 1u);
    EXPECT_EQ(canIds.count(280), 1u);
    EXPECT_EQ(canIds.count(599), 1u);
}

TEST(DemoTransportTest, FramesDecodeThroughTeslaDbcIntoVehicleSignals) {
    // The whole point: demo frames flow through the FULL seam and decode into
    // real VehicleSignals with plausible speed/throttle/gear/torque.
    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    DemoTransport transport(60);  // 20 loop iterations (3 frames each)
    ASSERT_TRUE(transport.open());
    RawFrameNormaliser normaliser;

    bool sawSpeed = false;
    bool sawThrottle = false;
    bool sawTorque = false;
    bool sawGear = false;
    int decoded = 0;

    auto stats = runReplay(transport, normaliser, service,
                           /*decodedSink=*/nullptr, /*rawSink=*/nullptr);
    (void)stats;

    // Re-run to inspect the decoded signals (runReplay consumed the transport).
    DemoTransport t2(60);
    ASSERT_TRUE(t2.open());
    while (auto line = t2.nextLine()) {
        auto r = normaliser.normalise(*line);
        if (r.kind != NormaliserResultKind::Frame) continue;
        auto bytes = toTwaiFrame(r.frame);
        // Live path: normaliser does not stamp a timestamp, so pass nullopt to
        // let the translator fall back to wall-clock now().
        auto sig = service.processFrame(bytes, std::nullopt);
        if (sig) {
            ++decoded;
            if (sig->getSpeedKmh().has_value()) sawSpeed = true;
            if (sig->getThrottlePercent().has_value()) sawThrottle = true;
            if (sig->getMotorTorqueNm().has_value()) sawTorque = true;
            if (sig->getGearSelector().has_value()) sawGear = true;

            // Wall-clock fallback: the emitted signal timestamp must be a
            // plausible recent epoch-ms value (within the last minute).
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
    // The demo loop ramps speed from ~0 up toward ~120 kph. Sampling early
    // vs late frames should show the ramp (a non-constant, increasing-then-
    // oscillating speed). We just assert the peak observed exceeds a modest
    // threshold so a regression to "always zero" is caught.
    DBCTranslationService service;
    DefaultVehicleConfigs::registerAll(service.registry());
    ASSERT_TRUE(service.loadVehicle("tesla", VehicleProtocol::CAN));

    DemoTransport t(120);
    ASSERT_TRUE(t.open());
    RawFrameNormaliser n;

    double peakSpeed = 0.0;
    while (auto line = t.nextLine()) {
        auto r = n.normalise(*line);
        if (r.kind != NormaliserResultKind::Frame) continue;
        if (r.frame.canId != 599) continue;  // only DI_speed carries speed
        auto bytes = toTwaiFrame(r.frame);
        auto sig = service.processFrame(bytes, std::nullopt);
        if (sig && sig->getSpeedKmh().has_value()) {
            peakSpeed = std::max(peakSpeed, *sig->getSpeedKmh());
        }
    }
    EXPECT_GT(peakSpeed, 50.0) << "demo speed should ramp well above 50 kph";
}

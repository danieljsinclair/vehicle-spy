// Integration: real bridge KeyboardInputProvider -> vehicle-sim adapter -> CSV row.
//
// These tests deliberately exercise the ACTUAL submodule code (no fake
// provider, no reimplemented mapping). If the bridge changes how a key is
// decoded, or the submodule goes missing, these fail — which is the point:
// they prove the reuse is real rather than a local copy that merely agrees.

#include "vehicle-sim/io/InteractiveCsvTelemetrySource.h"
#include "vehicle-sim/interactive/CsvKeyActionTarget.h"
#include "vehicle-sim/util/IClock.h"

#include "input/IKeyboardInput.h"

#include <gtest/gtest.h>

#include <chrono>
#include <initializer_list>
#include <memory>
#include <queue>

namespace {

// Scripted key source: yields queued keys, then -1 (the bridge's "no key"
// sentinel, which also terminates its per-frame drain loop).
class ScriptedKeyboard final : public ::IKeyboardInput {
public:
    explicit ScriptedKeyboard(std::queue<int> keys) : keys_{std::move(keys)} {}

    int getKey() override {
        int key = -1;
        if (!keys_.empty()) {
            key = keys_.front();
            keys_.pop();
        }
        return key;
    }

private:
    std::queue<int> keys_;
};

// Virtual clock: the production FakeClock from IClock.h advances virtual time
// on sleepFor instead of blocking, so no test here touches wall time.
using vehicle_sim::util::FakeClock;

std::unique_ptr<ScriptedKeyboard> keysOf(std::initializer_list<int> keys) {
    std::queue<int> q;
    for (int k : keys) {
        q.push(k);
    }
    return std::make_unique<ScriptedKeyboard>(std::move(q));
}

} // namespace

// --- The headline case the reuse exists to satisfy ---------------------------

TEST(BridgeKeyboardIntegration, DigitKeyReachesCsvRowAsThrottlePercent) {
    FakeClock clock;
    vehicle_sim::io::InteractiveCsvTelemetrySource source(
        keysOf({'5'}), clock, "veh-1", 20);

    const auto row = source.next();

    // '5' is decoded by the BRIDGE (setThrottleMomentary(0.5)); the adapter
    // scales it into the CSV's percent column.
    EXPECT_DOUBLE_EQ(row.throttle_percent, 50.0);
    EXPECT_EQ(row.vehicle_id, "veh-1");
}

TEST(BridgeKeyboardIntegration, ZeroKeyIsFullThrottleViaBridge) {
    FakeClock clock;
    vehicle_sim::io::InteractiveCsvTelemetrySource source(
        keysOf({'0'}), clock, "veh-1", 20);

    EXPECT_DOUBLE_EQ(source.next().throttle_percent, 100.0);
}

TEST(BridgeKeyboardIntegration, QuitKeyEndsTheSource) {
    FakeClock clock;
    vehicle_sim::io::InteractiveCsvTelemetrySource source(
        keysOf({'q'}), clock, "veh-1", 20);

    EXPECT_TRUE(source.hasNext());
    source.next();
    EXPECT_FALSE(source.hasNext());
}

// --- Derived columns must still be driven, not frozen ------------------------

TEST(BridgeKeyboardIntegration, ThrottleDrivesDerivedSpeedAndRpmColumns) {
    FakeClock clock;
    vehicle_sim::io::InteractiveCsvTelemetrySource source(
        keysOf({'5'}), clock, "veh-1", 20);

    const auto row = source.next();

    // Guards the regression we were worried about: a frozen throttle would
    // leave these at their idle defaults.
    EXPECT_GT(row.speed_kmh, 0.0);
    EXPECT_GT(row.motor_rpm, 800.0);
    EXPECT_GT(row.acceleration_g, 0.0);
}

// --- Steering: the concept the bridge does not model -------------------------

TEST(BridgeKeyboardIntegration, RightArrowSteersWithoutQuitting) {
    FakeClock clock;
    // Right arrow = ESC '[' 'C'. Fed raw to the bridge this would QUIT on the
    // ESC byte, so this asserts the adapter intercepts the sequence first.
    vehicle_sim::io::InteractiveCsvTelemetrySource source(
        keysOf({27, '[', 'C'}), clock, "veh-1", 20);

    const auto row = source.next();

    EXPECT_DOUBLE_EQ(row.steering_angle_deg, 5.0);
    EXPECT_TRUE(source.hasNext()) << "arrow key must not be read as quit";
}

TEST(BridgeKeyboardIntegration, LeftArrowSteersNegative) {
    FakeClock clock;
    vehicle_sim::io::InteractiveCsvTelemetrySource source(
        keysOf({27, '[', 'D'}), clock, "veh-1", 20);

    EXPECT_DOUBLE_EQ(source.next().steering_angle_deg, -5.0);
}

TEST(BridgeKeyboardIntegration, ArrowSequenceDoesNotSwallowFollowingKey) {
    FakeClock clock;
    // The filter must skip the 3 escape bytes and still hand '7' to the bridge
    // in the same frame — a naive filter returning -1 mid-drain would lose it.
    vehicle_sim::io::InteractiveCsvTelemetrySource source(
        keysOf({27, '[', 'C', '7'}), clock, "veh-1", 20);

    const auto row = source.next();

    EXPECT_DOUBLE_EQ(row.steering_angle_deg, 5.0);
    EXPECT_DOUBLE_EQ(row.throttle_percent, 70.0);
}

TEST(BridgeKeyboardIntegration, BareEscapeStillQuitsViaBridge) {
    FakeClock clock;
    // A lone ESC is not an arrow prefix, so it must reach the bridge and keep
    // its normal quit meaning rather than being silently eaten by the filter.
    vehicle_sim::io::InteractiveCsvTelemetrySource source(
        keysOf({27, 'x'}), clock, "veh-1", 20);

    source.next();

    EXPECT_FALSE(source.hasNext());
}

// --- Adapter unit-level: gear/brake come from the bridge's own callbacks -----

TEST(CsvKeyActionTargetTest, BrakeCallbackReleasesThrottle) {
    vehicle_sim::interactive::CsvKeyActionTarget target;

    target.setThrottleMomentary(0.8);
    target.setBrake(1.0);

    EXPECT_DOUBLE_EQ(target.state().brake_percent, 100.0);
    EXPECT_DOUBLE_EQ(target.state().throttle_percent, 0.0);
}

TEST(CsvKeyActionTargetTest, GearShiftsClampWithinRange) {
    vehicle_sim::interactive::CsvKeyActionTarget target;

    target.shiftDown();   // already in first
    EXPECT_EQ(target.state().gear, 1);

    target.shiftUp();
    EXPECT_EQ(target.state().gear, 2);
}

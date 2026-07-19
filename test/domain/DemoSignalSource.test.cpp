#include <gtest/gtest.h>
#include "vehicle-sim/domain/DemoSignalSource.h"
#include "vehicle-sim/domain/Gear.h"

using namespace vehicle_sim::domain;

// DemoSignalSource is constructed by SignalSourceFactory::create("demo", ...)
// (src/domain/SignalSourceFactory.cpp) — a production composition seam. The
// per-step signal math is exercised here via the pure computeNextSignal() seam
// (no thread / no clock), which generateSignals() routes through unchanged.

namespace {

// Helper: phase where cycle == target (cycle ∈ [0,1]); solvable since sin is surjective
// on [-1,1] and cycle maps monotonically from sin.
double phaseForCycle(double target) {
    return std::asin(2.0 * target - 1.0);  // target ∈ [0,1] → asin ∈ [-π/2, π/2]
}

} // namespace

class DemoSignalSourceMathTest : public ::testing::Test {
protected:
    // No fixtures needed: computeNextSignal is pure & static.
};

// Pins: full deterministic characterization at a single phase (midpoint
// cycle==0.5). Locks the entire formula chain end-to-end at one point and the
// cross-field relationships the simulator contract guarantees:
//   speed ≈ 1.25 * throttle  (100c vs 80c)
//   rpm   ≈ 1000 + 60 * speed (1000+6000c vs 100c)
TEST_F(DemoSignalSourceMathTest, ComputeNextSignal_MidpointCharacterizesFullChain) {
    const double phase = phaseForCycle(0.5);  // cycle == 0.5 exactly
    VehicleSignal s = DemoSignalSource::computeNextSignal(phase, 12345ULL);

    EXPECT_DOUBLE_EQ(s.getTimestampUtcMs(), 12345ULL);
    ASSERT_TRUE(s.getSpeedKmh().has_value());
    ASSERT_TRUE(s.getThrottlePercent().has_value());
    ASSERT_TRUE(s.getMotorRpm().has_value());

    // Cross-field invariants (not literal copies of single constants):
    EXPECT_NEAR(s.getSpeedKmh().value(), 1.25 * s.getThrottlePercent().value(), 1e-9);
    EXPECT_NEAR(s.getMotorRpm().value(), 1000.0 + 60.0 * s.getSpeedKmh().value(), 1e-9);

    // Midpoint composites (cycle 0.5): speed 50, throttle 40, rpm 4000.
    EXPECT_NEAR(s.getSpeedKmh().value(), 50.0, 1e-9);
    EXPECT_NEAR(s.getThrottlePercent().value(), 40.0, 1e-9);
    EXPECT_NEAR(s.getMotorRpm().value(), 4000.0, 1e-9);

    // Steering at midpoint: sin(phase*2) → sin(0) == 0 → 0 deg.
    ASSERT_TRUE(s.getSteeringAngleDeg().has_value());
    EXPECT_NEAR(s.getSteeringAngleDeg().value(), 0.0, 1e-9);

    // Gear at cycle 0.5 → index 2 → NEUTRAL.
    ASSERT_TRUE(s.getGearSelector().has_value());
    EXPECT_EQ(s.getGearSelector().value(), Gear::NEUTRAL);
}

// Pins: gear-select logic across ALL five canonical positions. The contract
// maps cycle quintiles to [PARK, REVERSE, NEUTRAL, AUTO_1, AUTO_2]; this
// verifies the boundary of each band (cycle at 0.0, 0.2, 0.4, 0.6, 0.8).
TEST_F(DemoSignalSourceMathTest, ComputeNextSignal_GearSelectsAllFivePositions) {
    const std::int32_t expectedByBand[] = {
        Gear::PARK, Gear::REVERSE, Gear::NEUTRAL, Gear::AUTO_1, Gear::AUTO_2
    };
    for (int band = 0; band < 5; ++band) {
        // Pick a cycle value squarely inside band `band` (cycle = band*0.2 + 0.1).
        const double cycle = band * 0.2 + 0.1;
        SCOPED_TRACE("gear band " + std::to_string(band) + " cycle=" + std::to_string(cycle));
        VehicleSignal s = DemoSignalSource::computeNextSignal(phaseForCycle(cycle), 1ULL);
        ASSERT_TRUE(s.getGearSelector().has_value());
        EXPECT_EQ(s.getGearSelector().value(), expectedByBand[band]);
    }
}

// Pins: gear-select upper clamp. cycle==1.0 (max) must map to index 4 (AUTO_2),
// not overrun the 5-element gear array.
TEST_F(DemoSignalSourceMathTest, ComputeNextSignal_GearClampsAtMaxCycle) {
    VehicleSignal s = DemoSignalSource::computeNextSignal(phaseForCycle(1.0), 1ULL);
    ASSERT_TRUE(s.getGearSelector().has_value());
    EXPECT_EQ(s.getGearSelector().value(), Gear::AUTO_2);
}

// Pins: signal formulas are monotonic & in-range vs cycle. Higher cycle ⇒
// higher speed/throttle/rpm. Asserts the ramp shape + valid ranges
// (not literal `cycle*K` copies).
TEST_F(DemoSignalSourceMathTest, ComputeNextSignal_SpeedThrottleRpmMonotonicInRange) {
    const double lowCycle = 0.25, highCycle = 0.75;
    VehicleSignal lo = DemoSignalSource::computeNextSignal(phaseForCycle(lowCycle), 1ULL);
    VehicleSignal hi = DemoSignalSource::computeNextSignal(phaseForCycle(highCycle), 1ULL);

    ASSERT_TRUE(lo.getSpeedKmh().has_value() && hi.getSpeedKmh().has_value());
    ASSERT_TRUE(lo.getThrottlePercent().has_value() && hi.getThrottlePercent().has_value());
    ASSERT_TRUE(lo.getMotorRpm().has_value() && hi.getMotorRpm().has_value());

    EXPECT_LT(lo.getSpeedKmh().value(), hi.getSpeedKmh().value());
    EXPECT_LT(lo.getThrottlePercent().value(), hi.getThrottlePercent().value());
    EXPECT_LT(lo.getMotorRpm().value(), hi.getMotorRpm().value());

    // Ranges per VehicleSignal contract.
    for (const VehicleSignal& s : {lo, hi}) {
        EXPECT_GE(s.getSpeedKmh().value(), VehicleSignal::SPEED_MIN);
        EXPECT_LE(s.getSpeedKmh().value(), VehicleSignal::SPEED_MAX);
        EXPECT_GE(s.getThrottlePercent().value(), VehicleSignal::THROTTLE_MIN);
        EXPECT_LE(s.getThrottlePercent().value(), VehicleSignal::THROTTLE_MAX);
        EXPECT_GE(s.getMotorRpm().value(), VehicleSignal::MOTOR_RPM_MIN);
        EXPECT_LE(s.getMotorRpm().value(), VehicleSignal::MOTOR_RPM_MAX);
    }
}

// Pins: brake branch logic. Brake is zero for cycle ≤ 0.7 and positive above;
// it is clamped to ≤ 100 at cycle==1.0. Asserts the branch + clamp behavior.
TEST_F(DemoSignalSourceMathTest, ComputeNextSignal_BrakeEngagesAboveCycle07AndClamps) {
    VehicleSignal below = DemoSignalSource::computeNextSignal(phaseForCycle(0.5), 1ULL);
    ASSERT_TRUE(below.getBrakePercent().has_value());
    EXPECT_DOUBLE_EQ(below.getBrakePercent().value(), 0.0);

    VehicleSignal above = DemoSignalSource::computeNextSignal(phaseForCycle(0.9), 1ULL);
    ASSERT_TRUE(above.getBrakePercent().has_value());
    EXPECT_GT(above.getBrakePercent().value(), 0.0);

    VehicleSignal maxc = DemoSignalSource::computeNextSignal(phaseForCycle(1.0), 1ULL);
    ASSERT_TRUE(maxc.getBrakePercent().has_value());
    EXPECT_LE(maxc.getBrakePercent().value(), VehicleSignal::BRAKE_MAX);
}

// Pins: motor-torque sign switch at cycle 0.6. Positive (drive) below,
// negative (regen/brake) at/above. This is the key regenerative-braking
// branch in the simulator contract.
TEST_F(DemoSignalSourceMathTest, ComputeNextSignal_MotorTorqueSwitchesSignAtCycle06) {
    VehicleSignal below = DemoSignalSource::computeNextSignal(phaseForCycle(0.3), 1ULL);
    ASSERT_TRUE(below.getMotorTorqueNm().has_value());
    EXPECT_GT(below.getMotorTorqueNm().value(), 0.0);

    VehicleSignal at = DemoSignalSource::computeNextSignal(phaseForCycle(0.6), 1ULL);
    ASSERT_TRUE(at.getMotorTorqueNm().has_value());
    EXPECT_LE(at.getMotorTorqueNm().value(), 0.0);

    VehicleSignal above = DemoSignalSource::computeNextSignal(phaseForCycle(0.9), 1ULL);
    ASSERT_TRUE(above.getMotorTorqueNm().has_value());
    EXPECT_LT(above.getMotorTorqueNm().value(), 0.0);
}

// Pins: phase periodicity / wrap continuity. generateSignals() wraps phase at
// 2π; the pure math must be exactly 2π-periodic, i.e. the signal at phase p
// equals the signal at p + 2π (sin periodicity). Guards against any
// phase-relative discontinuity in the math that would make the wrap jump.
TEST_F(DemoSignalSourceMathTest, ComputeNextSignal_Is2PiPeriodic) {
    const double p = 0.7;  // arbitrary phase away from a symmetry point
    VehicleSignal base = DemoSignalSource::computeNextSignal(p, 1ULL);
    VehicleSignal wrapped = DemoSignalSource::computeNextSignal(p + 2.0 * M_PI, 1ULL);

    ASSERT_TRUE(base.getSpeedKmh().has_value() && wrapped.getSpeedKmh().has_value());
    ASSERT_TRUE(base.getThrottlePercent().has_value() && wrapped.getThrottlePercent().has_value());
    EXPECT_NEAR(base.getSpeedKmh().value(), wrapped.getSpeedKmh().value(), 1e-9);
    EXPECT_NEAR(base.getThrottlePercent().value(), wrapped.getThrottlePercent().value(), 1e-9);
}

// Pins: HV voltage/current formulas. motorHvCurrent == |torque|/10 (the
// derived current), and voltage scales with cycle within the HV contract range.
TEST_F(DemoSignalSourceMathTest, ComputeNextSignal_HvVoltageAndDerivedCurrent) {
    const double cycle = 0.4;
    VehicleSignal s = DemoSignalSource::computeNextSignal(phaseForCycle(cycle), 1ULL);

    ASSERT_TRUE(s.getMotorTorqueNm().has_value());
    ASSERT_TRUE(s.getMotorHvCurrent().has_value());
    ASSERT_TRUE(s.getMotorHvVoltage().has_value());

    EXPECT_NEAR(s.getMotorHvCurrent().value(),
                std::abs(s.getMotorTorqueNm().value()) / 10.0, 1e-9);
    EXPECT_GE(s.getMotorHvVoltage().value(), VehicleSignal::HV_VOLTAGE_MIN);
    EXPECT_LE(s.getMotorHvVoltage().value(), VehicleSignal::HV_VOLTAGE_MAX);
}

// Pins: isRunning() default (false before start) + latestSignal() returns the
// default-constructed signal (timestamp 0) with no thread started. Covers the
// isRunning() accessor and the latestSignal() read path without invoking the
// sleep_for loop.
TEST_F(DemoSignalSourceMathTest, IsRunningFalseAndLatestSignalDefaultsBeforeStart) {
    DemoSignalSource source(50);  // interval only; thread NOT started.

    EXPECT_FALSE(source.isRunning());

    VehicleSignal s = source.latestSignal();
    EXPECT_EQ(s.getTimestampUtcMs(), 0ULL);
}
